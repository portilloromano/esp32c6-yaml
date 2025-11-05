#include "light_module.h"

#include "common/endpoint_utils.h"
#include "generated_config.h"

#include "common_macros.h"

#include <cstring>
#include <inttypes.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_matter_attribute.h>
#include <esp_matter_cluster.h>
#include <esp_matter_endpoint.h>
#include <led_indicator.h>
#include <lib/core/DataModelTypes.h>
#include <app-common/zap-generated/cluster-objects.h>

namespace device_modules::light {

using namespace esp_matter;
using namespace esp_matter::endpoint;
using namespace esp_matter::cluster;
using namespace esp_matter::attribute;
using namespace chip::app::Clusters;

namespace {

constexpr const char *TAG = "light_module";

struct identify_cluster_config {
    bool enabled;
    uint16_t identify_time;
    uint8_t identify_type;
};

struct groups_cluster_config {
    bool enabled;
};

struct scenes_management_cluster_config {
    bool enabled;
    uint16_t scene_table_size;
};

struct on_off_cluster_config {
    bool enabled;
    bool on;
    bool feature_lighting;
};

struct level_control_cluster_config {
    bool enabled;
    uint8_t current_level;
    uint8_t options;
    bool feature_on_off;
    bool feature_lighting;
    bool has_on_level;
    uint8_t on_level;
};

struct color_control_cluster_config {
    bool enabled;
    uint8_t color_mode;
    uint8_t enhanced_color_mode;
    bool has_current_hue;
    uint8_t current_hue;
    bool has_current_saturation;
    uint8_t current_saturation;
    bool has_color_temperature;
    uint16_t color_temperature_mireds;
    bool feature_color_temperature;
    bool feature_xy;
    bool has_remaining_time;
    uint16_t remaining_time;
};

struct endpoint_config_resolved {
    uint16_t id;
    const char *device_type;
    identify_cluster_config identify;
    groups_cluster_config groups;
    scenes_management_cluster_config scenes_management;
    on_off_cluster_config on_off;
    level_control_cluster_config level_control;
    color_control_cluster_config color_control;
};

constexpr int kStandardBrightness = 255;
constexpr int kStandardHue = 360;
constexpr int kStandardSaturation = 255;
constexpr uint32_t kStandardTemperatureFactor = 1'000'000;

constexpr int kMatterBrightness = 254;
constexpr int kMatterHue = 254;
constexpr int kMatterSaturation = 254;

app_driver_handle_t s_driver_handle = nullptr;
static bool s_is_identifying = false;

#if LED_STRIP_LED_COUNT > 0
static bool s_previous_on_off_state = false;
static led_indicator_ihsv_t s_previous_hsv_state = {0, 0, 0};
#endif

#if LED_STRIP_LED_COUNT > 0
static led_model_t resolve_led_model_from_config()
{
    const char *type = generated_config::led_strip::type;
    if (!type) {
        return LED_MODEL_WS2812;
    }

    if (strcmp(type, "sk6812") == 0 || strcmp(type, "sk6812_rgbw") == 0 || strcmp(type, "sk6812w") == 0) {
        return LED_MODEL_SK6812;
    }

    return LED_MODEL_WS2812;
}

static led_pixel_format_t resolve_pixel_format_from_config()
{
    const char *type = generated_config::led_strip::type;
    if (type && (strcmp(type, "sk6812w") == 0 || strcmp(type, "sk6812_rgbw") == 0 || strcmp(type, "rgbw") == 0)) {
        return LED_PIXEL_FORMAT_GRBW;
    }
    return LED_PIXEL_FORMAT_GRB;
}
#endif

static esp_err_t set_power(led_indicator_handle_t handle, esp_matter_attr_val_t *val)
{
#if LED_STRIP_LED_COUNT > 0
    return led_indicator_set_on_off(handle, val->val.b);
#else
    ESP_LOGI(TAG, "LED set power: %d (LED count is 0, visual update skipped)", val->val.b);
    return ESP_OK;
#endif
}

static esp_err_t set_brightness(led_indicator_handle_t handle, esp_matter_attr_val_t *val)
{
    int value = remap_to_range(val->val.u8, kMatterBrightness, kStandardBrightness);
#if LED_STRIP_LED_COUNT > 0
    return led_indicator_set_brightness(handle, value);
#else
    ESP_LOGI(TAG, "LED set brightness: %d (LED count is 0, visual update skipped)", value);
    return ESP_OK;
#endif
}

static esp_err_t set_hue(led_indicator_handle_t handle, esp_matter_attr_val_t *val)
{
    int value = remap_to_range(val->val.u8, kMatterHue, kStandardHue);
#if LED_STRIP_LED_COUNT > 0
    led_indicator_ihsv_t hsv;
    hsv.value = led_indicator_get_hsv(handle);
    led_indicator_ihsv_t new_hsv = {(uint32_t) value, hsv.s, hsv.v};
    return led_indicator_set_hsv(handle, new_hsv.value);
#else
    ESP_LOGI(TAG, "LED set hue: %d (LED count is 0, visual update skipped)", value);
    return ESP_OK;
#endif
}

static esp_err_t set_saturation(led_indicator_handle_t handle, esp_matter_attr_val_t *val)
{
    int value = remap_to_range(val->val.u8, kMatterSaturation, kStandardSaturation);
#if LED_STRIP_LED_COUNT > 0
    led_indicator_ihsv_t hsv;
    hsv.value = led_indicator_get_hsv(handle);
    led_indicator_ihsv_t new_hsv = {hsv.h, (uint8_t) value, hsv.v};
    return led_indicator_set_hsv(handle, new_hsv.value);
#else
    ESP_LOGI(TAG, "LED set saturation: %d (LED count is 0, visual update skipped)", value);
    return ESP_OK;
#endif
}

static esp_err_t set_temperature(led_indicator_handle_t handle, esp_matter_attr_val_t *val)
{
    uint32_t value = remap_to_range_inverse(val->val.u16, kStandardTemperatureFactor);
#if LED_STRIP_LED_COUNT > 0
    return led_indicator_set_color_temperature(handle, value);
#else
    ESP_LOGI(TAG, "LED set temperature: %ld (LED count is 0, visual update skipped)", value);
    return ESP_OK;
#endif
}

static esp_err_t set_default_brightness(uint16_t endpoint_id, led_indicator_handle_t handle)
{
    attribute_t *attribute = attribute::get(endpoint_id, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);
    if (!attribute) {
        ESP_LOGE(TAG, "Failed to get attribute LevelControl::CurrentLevel (ID: 0x%04X)!", (unsigned int) LevelControl::Attributes::CurrentLevel::Id);
        return ESP_FAIL;
    }

    esp_matter_attr_val_t val = esp_matter_invalid(nullptr);
    esp_err_t err = attribute::get_val(attribute, &val);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get CurrentLevel: %s", esp_err_to_name(err));
        return err;
    }
    return set_brightness(handle, &val);
}

static esp_err_t set_default_color(uint16_t endpoint_id, led_indicator_handle_t handle)
{
    attribute_t *mode_attr = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::ColorMode::Id);
    if (!mode_attr) {
        ESP_LOGE(TAG, "Failed to get ColorMode attribute for endpoint %u", endpoint_id);
        return ESP_FAIL;
    }

    esp_matter_attr_val_t mode = esp_matter_invalid(nullptr);
    esp_err_t err = attribute::get_val(mode_attr, &mode);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read ColorMode: %s", esp_err_to_name(err));
        return err;
    }

    if (mode.val.u8 == static_cast<uint8_t>(ColorControl::ColorModeEnum::kColorTemperatureMireds)) {
        attribute_t *temp_attr = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::ColorTemperatureMireds::Id);
        if (!temp_attr) {
            ESP_LOGE(TAG, "Missing ColorTemperatureMireds attribute at endpoint %u", endpoint_id);
            return ESP_FAIL;
        }
        esp_matter_attr_val_t temp = esp_matter_invalid(nullptr);
        err = attribute::get_val(temp_attr, &temp);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read ColorTemperatureMireds: %s", esp_err_to_name(err));
            return err;
        }
        return set_temperature(handle, &temp);
    }

    if (mode.val.u8 == static_cast<uint8_t>(ColorControl::ColorModeEnum::kCurrentHueAndCurrentSaturation)) {
        attribute_t *hue_attr = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentHue::Id);
        attribute_t *sat_attr = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentSaturation::Id);

        if (hue_attr) {
            esp_matter_attr_val_t hue = esp_matter_invalid(nullptr);
            err = attribute::get_val(hue_attr, &hue);
            if (err == ESP_OK) {
                err = set_hue(handle, &hue);
            } else {
                ESP_LOGE(TAG, "Failed to read CurrentHue: %s", esp_err_to_name(err));
            }
        }
        if (sat_attr) {
            esp_matter_attr_val_t sat = esp_matter_invalid(nullptr);
            esp_err_t sat_err = attribute::get_val(sat_attr, &sat);
            if (sat_err == ESP_OK) {
                sat_err = set_saturation(handle, &sat);
            } else {
                ESP_LOGE(TAG, "Failed to read CurrentSaturation: %s", esp_err_to_name(sat_err));
            }
            err |= sat_err;
        }
        return err;
    }

    ESP_LOGW(TAG, "Color mode 0x%02X not handled for defaults", mode.val.u8);
    return ESP_OK;
}

static esp_err_t set_default_power(uint16_t endpoint_id, led_indicator_handle_t handle)
{
    attribute_t *attribute = attribute::get(endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id);
    if (!attribute) {
        ESP_LOGE(TAG, "Failed to get OnOff attribute");
        return ESP_FAIL;
    }
    esp_matter_attr_val_t val = esp_matter_invalid(nullptr);
    esp_err_t err = attribute::get_val(attribute, &val);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read OnOff: %s", esp_err_to_name(err));
        return err;
    }
    return set_power(handle, &val);
}

static esp_err_t apply_light_defaults(uint16_t endpoint_id)
{
    esp_err_t err = ESP_OK;
    led_indicator_handle_t handle = (led_indicator_handle_t) endpoint::get_priv_data(endpoint_id);
    if (!handle) {
        handle = (led_indicator_handle_t) s_driver_handle;
    }

#if LED_STRIP_LED_COUNT == 0
    ESP_LOGW(TAG, "apply_light_defaults: LED strip disabled. Proceeding without LED operations.");
#endif

    err |= set_default_brightness(endpoint_id, handle);
    err |= set_default_color(endpoint_id, handle);
    err |= set_default_power(endpoint_id, handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error occurred while setting driver defaults for endpoint %u.", endpoint_id);
    } else {
        ESP_LOGI(TAG, "Driver defaults set successfully for endpoint %u.", endpoint_id);
    }
    return err;
}

bool streq(const char *lhs, const char *rhs)
{
    if (!lhs || !rhs) {
        return false;
    }
    return std::strcmp(lhs, rhs) == 0;
}

bool is_light_type(const char *device_type)
{
    return streq(device_type, "on_off_light") ||
           streq(device_type, "dimmable_light") ||
           streq(device_type, "extended_color_light");
}

bool list_contains(const generated_config::string_list &list, const char *value)
{
    if (!value || list.count == 0 || list.items == nullptr) {
        return false;
    }
    for (size_t idx = 0; idx < list.count; ++idx) {
        const char *item = list.items[idx];
        if (item && std::strcmp(item, value) == 0) {
            return true;
        }
    }
    return false;
}

bool device_has_default_cluster(const char *device_type, const char *cluster)
{
    if (streq(device_type, "on_off_light")) {
        return streq(cluster, "identify") ||
               streq(cluster, "groups") ||
               streq(cluster, "scenes_management") ||
               streq(cluster, "on_off");
    }
    if (streq(device_type, "dimmable_light")) {
        return streq(cluster, "identify") ||
               streq(cluster, "groups") ||
               streq(cluster, "scenes_management") ||
               streq(cluster, "on_off") ||
               streq(cluster, "level_control");
    }
    if (streq(device_type, "extended_color_light")) {
        return streq(cluster, "identify") ||
               streq(cluster, "groups") ||
               streq(cluster, "scenes_management") ||
               streq(cluster, "on_off") ||
               streq(cluster, "level_control") ||
               streq(cluster, "color_control");
    }
    return false;
}

bool default_feature_enabled(const char *device_type, const char *cluster, const char *feature)
{
    if (streq(device_type, "on_off_light")) {
        if (streq(cluster, "on_off")) {
            return streq(feature, "lighting");
        }
    } else if (streq(device_type, "dimmable_light")) {
        if (streq(cluster, "on_off")) {
            return streq(feature, "lighting");
        }
        if (streq(cluster, "level_control")) {
            return streq(feature, "on_off") || streq(feature, "lighting");
        }
    } else if (streq(device_type, "extended_color_light")) {
        if (streq(cluster, "on_off")) {
            return streq(feature, "lighting");
        }
        if (streq(cluster, "level_control")) {
            return streq(feature, "on_off") || streq(feature, "lighting");
        }
        if (streq(cluster, "color_control")) {
            return streq(feature, "color_temperature") || streq(feature, "xy");
        }
    }
    return false;
}

bool compute_enabled_flag(bool present, const generated_config::optional_bool &explicit_value, bool default_enabled)
{
    if (explicit_value.has_value) {
        return explicit_value.value;
    }
    if (present) {
        return true;
    }
    return default_enabled;
}

int optional_int_value(const generated_config::optional_int &value, int fallback)
{
    return value.has_value ? static_cast<int>(value.value) : fallback;
}

bool optional_bool_value(const generated_config::optional_bool &value, bool fallback)
{
    return value.has_value ? value.value : fallback;
}

uint8_t clamp_uint8(int value)
{
    if (value < 0) {
        return 0;
    }
    if (value > 255) {
        return 255;
    }
    return static_cast<uint8_t>(value);
}

uint16_t clamp_uint16(int value)
{
    if (value < 0) {
        return 0;
    }
    if (value > 65535) {
        return 65535;
    }
    return static_cast<uint16_t>(value);
}

bool feature_is_enabled(const char *device_type,
                        const char *cluster,
                        const generated_config::string_list &features,
                        const char *feature_name)
{
    if (list_contains(features, feature_name)) {
        return true;
    }
    return default_feature_enabled(device_type, cluster, feature_name);
}

chip::app::Clusters::ColorControl::ColorModeEnum map_color_mode_key(const char *key)
{
    using chip::app::Clusters::ColorControl::ColorModeEnum;
    if (!key) {
        return ColorModeEnum::kColorTemperatureMireds;
    }
    if (streq(key, "kColorTemperature") || streq(key, "kColorTemperatureMireds")) {
        return ColorModeEnum::kColorTemperatureMireds;
    }
    if (streq(key, "kCurrentHueAndCurrentSaturation")) {
        return ColorModeEnum::kCurrentHueAndCurrentSaturation;
    }
    if (streq(key, "kHueSaturation")) {
        return ColorModeEnum::kCurrentHueAndCurrentSaturation;
    }
    if (streq(key, "kCurrentXAndCurrentY") || streq(key, "kXY")) {
        return ColorModeEnum::kCurrentXAndCurrentY;
    }
    if (streq(key, "kUndefined") || streq(key, "kUnknownEnumValue")) {
        return ColorModeEnum::kUnknownEnumValue;
    }
    ESP_LOGW(TAG, "Unsupported color_mode '%s'; defaulting to kColorTemperatureMireds", key);
    return ColorModeEnum::kColorTemperatureMireds;
}

chip::app::Clusters::ColorControl::ColorModeEnum resolve_color_mode(const generated_config::optional_string &primary,
                                                                    const generated_config::optional_string &fallback)
{
    if (primary.has_value) {
        return map_color_mode_key(primary.value);
    }
    if (fallback.has_value) {
        return map_color_mode_key(fallback.value);
    }
    return chip::app::Clusters::ColorControl::ColorModeEnum::kColorTemperatureMireds;
}

endpoint_config_resolved resolve_light_config(const generated_config::endpoint_raw &raw)
{
    endpoint_config_resolved resolved{};
    resolved.id = raw.id;
    resolved.device_type = raw.device_type;

    resolved.identify.enabled = compute_enabled_flag(raw.identify.present,
                                                     raw.identify.enabled,
                                                     device_has_default_cluster(raw.device_type, "identify"));
    resolved.identify.identify_time = clamp_uint16(optional_int_value(raw.identify.identify_time, 0));
    resolved.identify.identify_type = clamp_uint8(optional_int_value(raw.identify.identify_type, 0));

    resolved.groups.enabled = compute_enabled_flag(raw.groups.present,
                                                   raw.groups.enabled,
                                                   device_has_default_cluster(raw.device_type, "groups"));

    resolved.scenes_management.enabled = compute_enabled_flag(raw.scenes_management.present,
                                                              raw.scenes_management.enabled,
                                                              device_has_default_cluster(raw.device_type, "scenes_management"));
    resolved.scenes_management.scene_table_size = clamp_uint16(optional_int_value(raw.scenes_management.scene_table_size, 16));

    resolved.on_off.enabled = compute_enabled_flag(raw.on_off.present,
                                                   raw.on_off.enabled,
                                                   device_has_default_cluster(raw.device_type, "on_off"));
    resolved.on_off.on = optional_bool_value(raw.on_off.state, false);
    resolved.on_off.feature_lighting = feature_is_enabled(raw.device_type, "on_off", raw.on_off.features, "lighting");

    resolved.level_control.enabled = compute_enabled_flag(raw.level_control.present,
                                                          raw.level_control.enabled,
                                                          device_has_default_cluster(raw.device_type, "level_control"));
    resolved.level_control.current_level = clamp_uint8(optional_int_value(raw.level_control.current_level, 0));
    resolved.level_control.options = clamp_uint8(optional_int_value(raw.level_control.options, 0));
    resolved.level_control.feature_on_off = feature_is_enabled(raw.device_type, "level_control", raw.level_control.features, "on_off");
    resolved.level_control.feature_lighting = feature_is_enabled(raw.device_type, "level_control", raw.level_control.features, "lighting");
    resolved.level_control.has_on_level = raw.level_control.on_level.has_value;
    resolved.level_control.on_level = clamp_uint8(optional_int_value(raw.level_control.on_level, 0));

    resolved.color_control.enabled = compute_enabled_flag(raw.color_control.present,
                                                          raw.color_control.enabled,
                                                          device_has_default_cluster(raw.device_type, "color_control"));

    generated_config::optional_string default_color_mode{true, "kColorTemperature"};
    auto base_mode = resolve_color_mode(raw.color_control.color_mode, default_color_mode);
    auto enhanced_mode = resolve_color_mode(raw.color_control.enhanced_color_mode,
                                            raw.color_control.color_mode.has_value ? raw.color_control.color_mode : default_color_mode);
    resolved.color_control.color_mode = static_cast<uint8_t>(base_mode);
    resolved.color_control.enhanced_color_mode = static_cast<uint8_t>(enhanced_mode);

    resolved.color_control.has_current_hue = raw.color_control.current_hue.has_value;
    resolved.color_control.current_hue = clamp_uint8(optional_int_value(raw.color_control.current_hue, 0));

    resolved.color_control.has_current_saturation = raw.color_control.current_saturation.has_value;
    resolved.color_control.current_saturation = clamp_uint8(optional_int_value(raw.color_control.current_saturation, 0));

    resolved.color_control.has_color_temperature = raw.color_control.color_temperature_mireds.has_value;
    resolved.color_control.color_temperature_mireds = clamp_uint16(optional_int_value(raw.color_control.color_temperature_mireds, 0));

    resolved.color_control.feature_color_temperature = feature_is_enabled(raw.device_type, "color_control", raw.color_control.features, "color_temperature");
    resolved.color_control.feature_xy = feature_is_enabled(raw.device_type, "color_control", raw.color_control.features, "xy");

    resolved.color_control.has_remaining_time = raw.color_control.remaining_time.has_value;
    resolved.color_control.remaining_time = clamp_uint16(optional_int_value(raw.color_control.remaining_time, 0));

    return resolved;
}

app_driver_handle_t init_drivers()
{
#if LED_STRIP_LED_COUNT > 0
    ESP_LOGI(TAG, "Initializing LED strip light driver...");
    static led_indicator_strips_config_t strips_config = {};
    strips_config.led_strip_cfg.strip_gpio_num = generated_config::led_strip::rmt_gpio;
    strips_config.led_strip_cfg.max_leds = LED_STRIP_LED_COUNT;
    strips_config.led_strip_cfg.led_pixel_format = resolve_pixel_format_from_config();
    strips_config.led_strip_cfg.led_model = resolve_led_model_from_config();
    strips_config.led_strip_cfg.flags.invert_out = 0;

    strips_config.led_strip_driver = LED_STRIP_RMT;
    strips_config.led_strip_rmt_cfg.clk_src = RMT_CLK_SRC_DEFAULT;
    strips_config.led_strip_rmt_cfg.resolution_hz = 10 * 1000 * 1000;
    strips_config.led_strip_rmt_cfg.mem_block_symbols = 64;
    strips_config.led_strip_rmt_cfg.flags.with_dma = false;

    led_indicator_config_t indicator_config = {
        .mode = LED_STRIPS_MODE,
        .led_indicator_strips_config = &strips_config,
        .blink_lists = nullptr,
        .blink_list_num = 0,
    };

    led_indicator_handle_t handle = led_indicator_create(&indicator_config);
    if (!handle) {
        ESP_LOGE(TAG, "Failed to create LED indicator for strip light.");
        s_driver_handle = nullptr;
    } else {
        s_driver_handle = handle;
    }
#else
    s_driver_handle = nullptr;
#endif
    return s_driver_handle;
}

bool supports_endpoint(const generated_config::endpoint_raw &config)
{
    return is_light_type(config.device_type);
}

template <typename ConfigT>
void apply_common_light_config(ConfigT &cfg, const endpoint_config_resolved &ep_config)
{
    if (ep_config.identify.enabled) {
        cfg.identify.identify_time = ep_config.identify.identify_time;
        cfg.identify.identify_type = ep_config.identify.identify_type;
    }
    if (ep_config.scenes_management.enabled) {
        cfg.scenes_management.scene_table_size = ep_config.scenes_management.scene_table_size;
    }
    if (ep_config.on_off.enabled) {
        cfg.on_off.on_off = ep_config.on_off.on;
    }
}

void apply_level_control_config(dimmable_light::config_t &cfg,
                                const level_control_cluster_config &level_cfg)
{
    if (!level_cfg.enabled) {
        cfg.level_control.current_level = nullptr;
        cfg.level_control.on_level = nullptr;
        return;
    }

    cfg.level_control.current_level = level_cfg.current_level;
    cfg.level_control.options = level_cfg.options;
    if (level_cfg.has_on_level) {
        cfg.level_control.on_level = level_cfg.on_level;
    } else {
        cfg.level_control.on_level = nullptr;
    }
}

void apply_color_control_config(extended_color_light::config_t &cfg,
                                const color_control_cluster_config &color_cfg)
{
    if (!color_cfg.enabled) {
        return;
    }

    cfg.color_control.color_mode = color_cfg.color_mode;
    cfg.color_control.enhanced_color_mode = color_cfg.enhanced_color_mode;
    if (color_cfg.feature_color_temperature && color_cfg.has_color_temperature) {
        cfg.color_control_color_temperature.color_temperature_mireds = color_cfg.color_temperature_mireds;
    }
    if (color_cfg.has_remaining_time) {
        cfg.color_control_remaining_time = color_cfg.remaining_time;
    }
}

template <typename ConfigT>
bool add_common_light_clusters(endpoint_t *endpoint,
                               ConfigT &cfg,
                               const endpoint_config_resolved &ep_config)
{
    if (ep_config.identify.enabled) {
        cluster_t *identify_cluster = identify::create(endpoint, &(cfg.identify), CLUSTER_FLAG_SERVER);
        if (!identify_cluster) {
            ESP_LOGE(TAG, "Failed to create identify cluster");
            return false;
        }
        identify::command::create_trigger_effect(identify_cluster);
    }

    if (ep_config.groups.enabled) {
        if (!groups::create(endpoint, &(cfg.groups), CLUSTER_FLAG_SERVER)) {
            ESP_LOGE(TAG, "Failed to create groups cluster");
            return false;
        }
    }

    if (ep_config.scenes_management.enabled) {
        if (!scenes_management::create(endpoint, &(cfg.scenes_management), CLUSTER_FLAG_SERVER)) {
            ESP_LOGE(TAG, "Failed to create scenes management cluster");
            return false;
        }
    }

    if (ep_config.on_off.enabled) {
        cluster_t *on_off_cluster = on_off::create(endpoint, &(cfg.on_off), CLUSTER_FLAG_SERVER);
        if (!on_off_cluster) {
            ESP_LOGE(TAG, "Failed to create on/off cluster");
            return false;
        }
        if (ep_config.on_off.feature_lighting) {
            on_off::feature::lighting::add(on_off_cluster, &(cfg.on_off_lighting));
        }
        on_off::command::create_on(on_off_cluster);
        on_off::command::create_toggle(on_off_cluster);
    }

    return true;
}

bool add_level_control_cluster(endpoint_t *endpoint,
                               dimmable_light::config_t &cfg,
                               const level_control_cluster_config &level_cfg)
{
    if (!level_cfg.enabled) {
        return true;
    }

    cluster_t *level_cluster = level_control::create(endpoint, &(cfg.level_control), CLUSTER_FLAG_SERVER);
    if (!level_cluster) {
        ESP_LOGE(TAG, "Failed to create level control cluster");
        return false;
    }

    if (level_cfg.feature_on_off) {
        level_control::feature::on_off::add(level_cluster);
    }
    if (level_cfg.feature_lighting) {
        level_control::feature::lighting::add(level_cluster, &(cfg.level_control_lighting));
    }

    return true;
}

bool add_color_control_cluster(endpoint_t *endpoint,
                               extended_color_light::config_t &cfg,
                               const color_control_cluster_config &color_cfg)
{
    if (!color_cfg.enabled) {
        return true;
    }

    cluster_t *color_cluster = color_control::create(endpoint, &(cfg.color_control), CLUSTER_FLAG_SERVER);
    if (!color_cluster) {
        ESP_LOGE(TAG, "Failed to create color control cluster");
        return false;
    }

    if (color_cfg.feature_color_temperature) {
        color_control::feature::color_temperature::add(color_cluster, &(cfg.color_control_color_temperature));
    }
    if (color_cfg.feature_xy) {
        color_control::feature::xy::add(color_cluster, &(cfg.color_control_xy));
    }

    color_control::attribute::create_remaining_time(color_cluster, cfg.color_control_remaining_time);
    color_control::command::create_stop_move_step(color_cluster);

    return true;
}

endpoint_t *create_on_off_light_endpoint(const endpoint_config_resolved &ep_config, node_t *node)
{
    on_off_light::config_t cfg;
    apply_common_light_config(cfg, ep_config);

    endpoint_t *endpoint = endpoint::create(node, ENDPOINT_FLAG_NONE, s_driver_handle);
    if (!endpoint) {
        ESP_LOGE(TAG, "Failed to allocate endpoint for device type %s", ep_config.device_type);
        return nullptr;
    }

    if (!utils::add_descriptor_and_register(endpoint, cfg.descriptor, ep_config.device_type)) {
        return nullptr;
    }

    if (!add_common_light_clusters(endpoint, cfg, ep_config)) {
        return nullptr;
    }

    return endpoint;
}

endpoint_t *create_dimmable_light_endpoint(const endpoint_config_resolved &ep_config, node_t *node)
{
    dimmable_light::config_t cfg;
    apply_common_light_config(cfg, ep_config);
    apply_level_control_config(cfg, ep_config.level_control);

    endpoint_t *endpoint = endpoint::create(node, ENDPOINT_FLAG_NONE, s_driver_handle);
    if (!endpoint) {
        ESP_LOGE(TAG, "Failed to allocate endpoint for device type %s", ep_config.device_type);
        return nullptr;
    }

    if (!utils::add_descriptor_and_register(endpoint, cfg.descriptor, ep_config.device_type)) {
        return nullptr;
    }

    if (!add_common_light_clusters(endpoint, cfg, ep_config)) {
        return nullptr;
    }

    if (!add_level_control_cluster(endpoint, cfg, ep_config.level_control)) {
        return nullptr;
    }

    return endpoint;
}

endpoint_t *create_extended_color_light_endpoint(const endpoint_config_resolved &ep_config, node_t *node)
{
    extended_color_light::config_t cfg;
    apply_common_light_config(cfg, ep_config);
    apply_level_control_config(cfg, ep_config.level_control);
    apply_color_control_config(cfg, ep_config.color_control);

    endpoint_t *endpoint = endpoint::create(node, ENDPOINT_FLAG_NONE, s_driver_handle);
    if (!endpoint) {
        ESP_LOGE(TAG, "Failed to allocate endpoint for device type %s", ep_config.device_type);
        return nullptr;
    }

    if (!utils::add_descriptor_and_register(endpoint, cfg.descriptor, ep_config.device_type)) {
        return nullptr;
    }

    if (!add_common_light_clusters(endpoint, cfg, ep_config)) {
        return nullptr;
    }

    if (!add_level_control_cluster(endpoint, cfg, ep_config.level_control)) {
        return nullptr;
    }

    if (!add_color_control_cluster(endpoint, cfg, ep_config.color_control)) {
        return nullptr;
    }

    return endpoint;
}

endpoint_t *create_endpoint(const generated_config::endpoint_raw &config, node_t *node)
{
    if (!is_light_type(config.device_type)) {
        return nullptr;
    }

    endpoint_config_resolved resolved = resolve_light_config(config);

    if (streq(config.device_type, "extended_color_light")) {
        return create_extended_color_light_endpoint(resolved, node);
    }
    if (streq(config.device_type, "dimmable_light")) {
        return create_dimmable_light_endpoint(resolved, node);
    }
    if (streq(config.device_type, "on_off_light")) {
        return create_on_off_light_endpoint(resolved, node);
    }
    return nullptr;
}

esp_err_t attribute_update(app_driver_handle_t driver_handle,
                           uint16_t endpoint_id,
                           uint32_t cluster_id,
                           uint32_t attribute_id,
                           esp_matter_attr_val_t *val)
{
    ESP_LOGI(TAG,
             "Updating attribute - Cluster: 0x%" PRIx32 ", Attribute: 0x%" PRIx32 ", Value: %d",
             cluster_id,
             attribute_id,
             val->val.u8);

    if (endpoint_id != light_endpoint_id) {
        return ESP_OK;
    }

    led_indicator_handle_t handle = static_cast<led_indicator_handle_t>(driver_handle);

    if (cluster_id == OnOff::Id) {
        if (attribute_id == OnOff::Attributes::OnOff::Id) {
            return set_power(handle, val);
        }
    } else if (cluster_id == LevelControl::Id) {
        if (attribute_id == LevelControl::Attributes::CurrentLevel::Id) {
            return set_brightness(handle, val);
        }
    } else if (cluster_id == ColorControl::Id) {
        if (attribute_id == ColorControl::Attributes::CurrentHue::Id) {
            return set_hue(handle, val);
        }
        if (attribute_id == ColorControl::Attributes::CurrentSaturation::Id) {
            return set_saturation(handle, val);
        }
        if (attribute_id == ColorControl::Attributes::ColorTemperatureMireds::Id) {
            return set_temperature(handle, val);
        }
    }
    return ESP_OK;
}

void after_endpoint_created(const generated_config::endpoint_raw &config, endpoint_t *endpoint)
{
    if (!endpoint || !is_light_type(config.device_type)) {
        return;
    }

    const uint16_t endpoint_id = endpoint::get_id(endpoint);
    if (light_endpoint_id == chip::kInvalidEndpointId) {
        light_endpoint_id = endpoint_id;
    }

    endpoint_config_resolved resolved = resolve_light_config(config);
    if (resolved.color_control.enabled) {
        esp_matter_attr_val_t val;
        if (resolved.color_control.has_color_temperature) {
            val = esp_matter_uint16(resolved.color_control.color_temperature_mireds);
            attribute::update(endpoint_id,
                              ColorControl::Id,
                              ColorControl::Attributes::ColorTemperatureMireds::Id,
                              &val);
        }
        if (resolved.color_control.has_current_hue) {
            val = esp_matter_uint8(resolved.color_control.current_hue);
            attribute::update(endpoint_id,
                              ColorControl::Id,
                              ColorControl::Attributes::CurrentHue::Id,
                              &val);
        }
        if (resolved.color_control.has_current_saturation) {
            val = esp_matter_uint8(resolved.color_control.current_saturation);
            attribute::update(endpoint_id,
                              ColorControl::Id,
                              ColorControl::Attributes::CurrentSaturation::Id,
                              &val);
        }
    }

    if (resolved.on_off.enabled) {
        esp_matter_attr_val_t val = esp_matter_bool(resolved.on_off.on);
        attribute::update(endpoint_id,
                          OnOff::Id,
                          OnOff::Attributes::OnOff::Id,
                          &val);
    }

    if (resolved.level_control.enabled) {
        esp_matter_attr_val_t val = esp_matter_uint8(resolved.level_control.current_level);
        attribute::update(endpoint_id,
                          LevelControl::Id,
                          LevelControl::Attributes::CurrentLevel::Id,
                          &val);
    }
}

void perform_identification(app_driver_handle_t driver_handle,
                            esp_matter::identification::callback_type_t type,
                            uint8_t effect_id)
{
    ESP_LOGI(TAG, "Identify action: Type=%d, EffectID=0x%02x", static_cast<int>(type), effect_id);

    if (type == esp_matter::identification::START) {
        if (s_is_identifying) {
            ESP_LOGI(TAG, "Identify: Already identifying. Ignoring new START.");
            return;
        }
        s_is_identifying = true;
    }

#if LED_STRIP_LED_COUNT > 0
    led_indicator_handle_t handle = static_cast<led_indicator_handle_t>(driver_handle);
    if (!handle) {
        ESP_LOGE(TAG, "Identify: Invalid LED strip driver handle.");
        return;
    }

    if (type == esp_matter::identification::START) {
        ESP_LOGI(TAG, "Identify: Saving current LED state before starting identification.");
        uint8_t current_brightness = led_indicator_get_brightness(handle);
        s_previous_on_off_state = (current_brightness > 0);
        s_previous_hsv_state.value = led_indicator_get_hsv(handle);

        ESP_LOGD(TAG,
                 "Identify: State saved. Prev OnOff: %d, Prev H: %d, S: %d, V: %d, Brightness: %d",
                 s_previous_on_off_state,
                 s_previous_hsv_state.h,
                 s_previous_hsv_state.s,
                 s_previous_hsv_state.v,
                 current_brightness);

        ESP_LOGI(TAG, "Identify: Setting LED to full brightness for identification (no blink support).");
        esp_err_t err_set = led_indicator_set_brightness(handle, kStandardBrightness);
        if (err_set != ESP_OK) {
            ESP_LOGE(TAG, "Identify: Failed to set LED brightness for identification: %s", esp_err_to_name(err_set));
        }
    } else if (type == esp_matter::identification::STOP) {
        if (s_is_identifying) {
            ESP_LOGI(TAG, "Identify: Stopping identification and restoring previous LED state.");

            esp_err_t err_hsv = led_indicator_set_hsv(handle, s_previous_hsv_state.value);
            if (err_hsv != ESP_OK) {
                ESP_LOGE(TAG, "Identify: Failed to restore HSV state: %s", esp_err_to_name(err_hsv));
            }

            esp_err_t err_on_off = led_indicator_set_on_off(handle, s_previous_on_off_state);
            if (err_on_off != ESP_OK) {
                ESP_LOGE(TAG, "Identify: Failed to restore on/off state: %s", esp_err_to_name(err_on_off));
            }
            ESP_LOGI(TAG, "Identify: Previous LED state restoration attempted.");
            s_is_identifying = false;
        } else {
            ESP_LOGI(TAG, "Identify STOP received, but was not actively identifying with LEDs.");
        }
    }
#else
    ESP_LOGI(TAG, "LED strip disabled. Visual identification skipped.");
    if (type == esp_matter::identification::STOP) {
        s_is_identifying = false;
    }
#endif
}

void apply_post_stack_start()
{
    if (light_endpoint_id != chip::kInvalidEndpointId) {
        esp_err_t err = apply_light_defaults(light_endpoint_id);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Driver defaults set for light endpoint %u.", light_endpoint_id);
        } else {
            ESP_LOGE(TAG, "Failed to set driver defaults for light endpoint %u: %s",
                     light_endpoint_id,
                     esp_err_to_name(err));
        }
    }
}

} // namespace

uint16_t light_endpoint_id = chip::kInvalidEndpointId;

const DeviceModule kModule = {
    .name = "light",
    .init_drivers = init_drivers,
    .supports_endpoint = supports_endpoint,
    .create_endpoint = create_endpoint,
    .after_endpoint_created = after_endpoint_created,
    .apply_post_stack_start = apply_post_stack_start,
    .attribute_update = attribute_update,
    .perform_identification = perform_identification,
};

} // namespace device_modules::light
