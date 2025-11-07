#include "extended_color_light.h"

#include "device_modules/common/endpoint_utils.h"
#include "generated_config.h"

#include <app-common/zap-generated/cluster-objects.h>
#include <esp_mac.h>
#include <esp_matter_attribute.h>
#include <esp_matter_cluster.h>
#include <esp_matter_endpoint.h>
#include <lib/core/CHIPError.h>
#include <lib/core/NodeId.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/Span.h>
#include <lib/support/TypeTraits.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/CommissionableDataProvider.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace device_modules::extended_color_light {

using namespace esp_matter;
using namespace esp_matter::cluster;

namespace {

struct identify_config {
    bool enabled;
    uint16_t identify_time;
    uint8_t identify_type;
};

struct groups_config {
    bool enabled;
};

struct scenes_config {
    bool enabled;
    uint16_t scene_table_size;
};

struct on_off_config {
    bool enabled;
    bool on;
    bool feature_lighting;
};

struct level_config {
    bool enabled;
    uint8_t current_level;
    uint8_t options;
    bool feature_on_off;
    bool feature_lighting;
    bool has_on_level;
    uint8_t on_level;
};

struct color_config {
    bool enabled;
    uint8_t color_mode;
    uint8_t enhanced_color_mode;
    bool has_color_temperature;
    uint16_t color_temperature_mireds;
    uint16_t min_mireds;
    uint16_t max_mireds;
    bool has_remaining_time;
    uint16_t remaining_time;
    bool feature_color_temperature;
};

struct endpoint_config {
    uint16_t id;
    const char *device_type;
    identify_config identify;
    groups_config groups;
    scenes_config scenes_management;
    on_off_config on_off;
    level_config level_control;
    color_config color_control;
};

class MacDerivedCommissionableDataProvider : public chip::DeviceLayer::CommissionableDataProvider {
public:
    MacDerivedCommissionableDataProvider() : mBase(nullptr), mPasscode(20202021), mDiscriminator(3840) {}
    void SetBase(chip::DeviceLayer::CommissionableDataProvider *base) { mBase = base; }
    void SetCredentials(uint16_t discriminator, uint32_t passcode)
    {
        mDiscriminator = discriminator;
        mPasscode = passcode;
    }
    CHIP_ERROR GetSetupDiscriminator(uint16_t &setupDiscriminator) override
    {
        setupDiscriminator = mDiscriminator;
        return CHIP_NO_ERROR;
    }
    CHIP_ERROR SetSetupDiscriminator(uint16_t setupDiscriminator) override
    {
        mDiscriminator = setupDiscriminator;
        return mBase ? mBase->SetSetupDiscriminator(setupDiscriminator) : CHIP_NO_ERROR;
    }
    CHIP_ERROR GetSpake2pIterationCount(uint32_t &iterationCount) override
    {
        return mBase ? mBase->GetSpake2pIterationCount(iterationCount) : CHIP_ERROR_NOT_IMPLEMENTED;
    }
    CHIP_ERROR GetSpake2pSalt(chip::MutableByteSpan &saltBuf) override
    {
        return mBase ? mBase->GetSpake2pSalt(saltBuf) : CHIP_ERROR_NOT_IMPLEMENTED;
    }
    CHIP_ERROR GetSpake2pVerifier(chip::MutableByteSpan &verifierBuf, size_t &verifierLen) override
    {
        return mBase ? mBase->GetSpake2pVerifier(verifierBuf, verifierLen) : CHIP_ERROR_NOT_IMPLEMENTED;
    }
    CHIP_ERROR GetSetupPasscode(uint32_t &setupPasscode) override
    {
        setupPasscode = mPasscode;
        return CHIP_NO_ERROR;
    }
    CHIP_ERROR SetSetupPasscode(uint32_t setupPasscode) override
    {
        mPasscode = setupPasscode;
        return mBase ? mBase->SetSetupPasscode(setupPasscode) : CHIP_NO_ERROR;
    }
private:
    chip::DeviceLayer::CommissionableDataProvider *mBase;
    uint32_t mPasscode;
    uint16_t mDiscriminator;
};

MacDerivedCommissionableDataProvider g_provider;
bool g_provider_registered = false;

bool strings_equal(const char *a, const char *b)
{
    if (!a || !b) {
        return false;
    }
    return std::strcmp(a, b) == 0;
}

bool list_contains(const generated_config::string_list &list, const char *value)
{
    if (!value) {
        return false;
    }
    for (size_t i = 0; i < list.count; ++i) {
        const char *item = list.items[i];
        if (item && std::strcmp(item, value) == 0) {
            return true;
        }
    }
    return false;
}

bool compute_enabled(bool present, const generated_config::optional_bool &flag, bool default_enabled)
{
    if (flag.has_value) {
        return flag.value;
    }
    if (present) {
        return true;
    }
    return default_enabled;
}

uint8_t clamp_uint8(int value, uint8_t min_value, uint8_t max_value)
{
    if (value < static_cast<int>(min_value)) {
        return min_value;
    }
    if (value > static_cast<int>(max_value)) {
        return max_value;
    }
    return static_cast<uint8_t>(value);
}

uint8_t parse_color_mode(const generated_config::optional_string &value, uint8_t fallback)
{
    if (!value.has_value || !value.value) {
        return fallback;
    }
    if (strings_equal(value.value, "kColorTemperature") || strings_equal(value.value, "kColorTemperatureMireds")) {
        return static_cast<uint8_t>(chip::to_underlying(chip::app::Clusters::ColorControl::ColorModeEnum::kColorTemperatureMireds));
    }
    if (strings_equal(value.value, "kCurrentHueAndCurrentSaturation")) {
        return static_cast<uint8_t>(chip::to_underlying(chip::app::Clusters::ColorControl::ColorModeEnum::kCurrentHueAndCurrentSaturation));
    }
    if (strings_equal(value.value, "kCurrentXAndCurrentY")) {
        return static_cast<uint8_t>(chip::to_underlying(chip::app::Clusters::ColorControl::ColorModeEnum::kCurrentXAndCurrentY));
    }
    return fallback;
}

uint8_t parse_enhanced_mode(const generated_config::optional_string &value, uint8_t fallback)
{
    if (!value.has_value || !value.value) {
        return fallback;
    }
    if (strings_equal(value.value, "kColorTemperature") || strings_equal(value.value, "kColorTemperatureMireds")) {
        return static_cast<uint8_t>(chip::to_underlying(chip::app::Clusters::ColorControl::EnhancedColorModeEnum::kColorTemperatureMireds));
    }
    if (strings_equal(value.value, "kCurrentHueAndCurrentSaturation")) {
        return static_cast<uint8_t>(chip::to_underlying(chip::app::Clusters::ColorControl::EnhancedColorModeEnum::kCurrentHueAndCurrentSaturation));
    }
    if (strings_equal(value.value, "kEnhancedCurrentHueAndCurrentSaturation")) {
        return static_cast<uint8_t>(chip::to_underlying(chip::app::Clusters::ColorControl::EnhancedColorModeEnum::kEnhancedCurrentHueAndCurrentSaturation));
    }
    if (strings_equal(value.value, "kCurrentXAndCurrentY")) {
        return static_cast<uint8_t>(chip::to_underlying(chip::app::Clusters::ColorControl::EnhancedColorModeEnum::kCurrentXAndCurrentY));
    }
    return fallback;
}

endpoint_config resolve_config(const generated_config::endpoint_raw &raw)
{
    endpoint_config resolved{};
    resolved.id = raw.id;
    resolved.device_type = raw.device_type;
    resolved.identify.enabled = compute_enabled(raw.identify.present, raw.identify.enabled, true);
    resolved.identify.identify_time = raw.identify.identify_time.has_value ? static_cast<uint16_t>(raw.identify.identify_time.value) : 0;
    resolved.identify.identify_type = raw.identify.identify_type.has_value ? static_cast<uint8_t>(raw.identify.identify_type.value) : 0;
    resolved.groups.enabled = compute_enabled(raw.groups.present, raw.groups.enabled, false);
    resolved.scenes_management.enabled = compute_enabled(raw.scenes_management.present, raw.scenes_management.enabled, false);
    resolved.scenes_management.scene_table_size = raw.scenes_management.scene_table_size.has_value ? static_cast<uint16_t>(raw.scenes_management.scene_table_size.value) : 0;
    resolved.on_off.enabled = compute_enabled(raw.on_off.present, raw.on_off.enabled, true);
    resolved.on_off.on = raw.on_off.state.has_value ? raw.on_off.state.value : false;
    resolved.on_off.feature_lighting = list_contains(raw.on_off.features, "lighting");
    resolved.level_control.enabled = compute_enabled(raw.level_control.present, raw.level_control.enabled, true);
    resolved.level_control.current_level = clamp_uint8(raw.level_control.current_level.has_value ? raw.level_control.current_level.value : 254, 0, 254);
    resolved.level_control.options = clamp_uint8(raw.level_control.options.has_value ? raw.level_control.options.value : 0, 0, 255);
    resolved.level_control.has_on_level = raw.level_control.on_level.has_value;
    resolved.level_control.on_level = clamp_uint8(raw.level_control.on_level.has_value ? raw.level_control.on_level.value : 0, 0, 254);
    resolved.level_control.feature_on_off = list_contains(raw.level_control.features, "on_off");
    resolved.level_control.feature_lighting = list_contains(raw.level_control.features, "lighting");
    resolved.color_control.enabled = compute_enabled(raw.color_control.present, raw.color_control.enabled, true);
    resolved.color_control.color_mode = parse_color_mode(raw.color_control.color_mode, static_cast<uint8_t>(chip::to_underlying(chip::app::Clusters::ColorControl::ColorModeEnum::kColorTemperatureMireds)));
    resolved.color_control.enhanced_color_mode = parse_enhanced_mode(raw.color_control.enhanced_color_mode, static_cast<uint8_t>(chip::to_underlying(chip::app::Clusters::ColorControl::EnhancedColorModeEnum::kColorTemperatureMireds)));
    resolved.color_control.has_color_temperature = raw.color_control.color_temperature_mireds.has_value;
    resolved.color_control.color_temperature_mireds = raw.color_control.color_temperature_mireds.has_value ? static_cast<uint16_t>(raw.color_control.color_temperature_mireds.value) : 350;
    resolved.color_control.min_mireds = 153;
    resolved.color_control.max_mireds = 500;
    resolved.color_control.has_remaining_time = raw.color_control.remaining_time.has_value;
    resolved.color_control.remaining_time = raw.color_control.remaining_time.has_value ? static_cast<uint16_t>(raw.color_control.remaining_time.value) : 0;
    resolved.color_control.feature_color_temperature = true;
    return resolved;
}

bool read_mac(uint8_t (&mac)[6])
{
    if (esp_base_mac_addr_get(mac) == ESP_OK) {
        return true;
    }
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        return true;
    }
    return false;
}

void register_provider()
{
    if (g_provider_registered) {
        return;
    }
    chip::DeviceLayer::CommissionableDataProvider *base = chip::DeviceLayer::GetCommissionableDataProvider();
    g_provider.SetBase(base);
    uint16_t discriminator = 3840;
    uint32_t passcode = 20202021;
    uint8_t mac[6] = {0};
    if (read_mac(mac)) {
        uint64_t mac_value = 0;
        for (int i = 0; i < 6; ++i) {
            mac_value = (mac_value << 8) | mac[i];
        }
        uint64_t mix = mac_value ^ (mac_value >> 12) ^ 0x5A5A5A5A5A5AULL;
        uint32_t pin_raw = static_cast<uint32_t>(mix % 99999999ULL);
        passcode = 10000000U + (pin_raw % 89999999U);
        discriminator = static_cast<uint16_t>((mac_value ^ (mac_value >> 24)) & 0x0FFFU);
    }
    g_provider.SetCredentials(discriminator, passcode);
    chip::DeviceLayer::SetCommissionableDataProvider(&g_provider);
    g_provider_registered = true;
}

}

uint16_t extended_color_light_endpoint_id = chip::kInvalidEndpointId;

app_driver_handle_t init_drivers()
{
    register_provider();
    return nullptr;
}

bool supports_endpoint(const generated_config::endpoint_raw &config)
{
    return strings_equal(config.device_type, "extended_color_light");
}

endpoint_t *create_endpoint(const generated_config::endpoint_raw &config, node_t *node)
{
    if (!supports_endpoint(config)) {
        return nullptr;
    }
    endpoint_config resolved = resolve_config(config);
    endpoint_t *endpoint = endpoint::create(node, ENDPOINT_FLAG_NONE, nullptr);
    if (!endpoint) {
        return nullptr;
    }
    cluster::descriptor::config_t descriptor_config;
    if (!device_modules::utils::add_descriptor_and_register(endpoint, descriptor_config, resolved.device_type)) {
        return nullptr;
    }
    if (resolved.identify.enabled) {
        cluster::identify::config_t identify_cfg;
        identify_cfg.identify_time = resolved.identify.identify_time;
        identify_cfg.identify_type = resolved.identify.identify_type;
        if (!identify::create(endpoint, &identify_cfg, CLUSTER_FLAG_SERVER)) {
            return nullptr;
        }
    }
    if (resolved.groups.enabled) {
        cluster::groups::config_t groups_cfg;
        if (!groups::create(endpoint, &groups_cfg, CLUSTER_FLAG_SERVER)) {
            return nullptr;
        }
    }
    if (resolved.scenes_management.enabled) {
        cluster::scenes_management::config_t scenes_cfg;
        scenes_cfg.scene_table_size = resolved.scenes_management.scene_table_size;
        if (!scenes_management::create(endpoint, &scenes_cfg, CLUSTER_FLAG_SERVER)) {
            return nullptr;
        }
    }
    if (resolved.on_off.enabled) {
        cluster::on_off::config_t on_off_cfg;
        on_off_cfg.on_off = resolved.on_off.on;
        cluster_t *on_off_cluster = on_off::create(endpoint, &on_off_cfg, CLUSTER_FLAG_SERVER);
        if (!on_off_cluster) {
            return nullptr;
        }
        if (resolved.on_off.feature_lighting) {
            cluster::on_off::feature::lighting::config_t lighting_cfg;
            on_off::feature::lighting::add(on_off_cluster, &lighting_cfg);
        }
        on_off::command::create_on(on_off_cluster);
        on_off::command::create_off(on_off_cluster);
        on_off::command::create_toggle(on_off_cluster);
    }
    if (resolved.level_control.enabled) {
        cluster::level_control::config_t level_cfg;
        level_cfg.current_level = resolved.level_control.current_level;
        level_cfg.options = resolved.level_control.options;
        if (resolved.level_control.has_on_level) {
            level_cfg.on_level = resolved.level_control.on_level;
        } else {
            level_cfg.on_level = nullptr;
        }
        cluster_t *level_cluster = level_control::create(endpoint, &level_cfg, CLUSTER_FLAG_SERVER);
        if (!level_cluster) {
            return nullptr;
        }
        if (resolved.level_control.feature_on_off) {
            level_control::feature::on_off::add(level_cluster);
        }
        if (resolved.level_control.feature_lighting) {
            cluster::level_control::feature::lighting::config_t level_lighting_cfg;
            level_control::feature::lighting::add(level_cluster, &level_lighting_cfg);
        }
    }
    if (resolved.color_control.enabled) {
        cluster::color_control::config_t color_cfg;
        color_cfg.color_mode = resolved.color_control.color_mode;
        color_cfg.enhanced_color_mode = resolved.color_control.enhanced_color_mode;
        cluster_t *color_cluster = color_control::create(endpoint, &color_cfg, CLUSTER_FLAG_SERVER);
        if (!color_cluster) {
            return nullptr;
        }
        if (resolved.color_control.feature_color_temperature) {
            cluster::color_control::feature::color_temperature::config_t temp_cfg;
            temp_cfg.color_temperature_mireds = resolved.color_control.color_temperature_mireds;
            temp_cfg.color_temp_physical_min_mireds = resolved.color_control.min_mireds;
            temp_cfg.color_temp_physical_max_mireds = resolved.color_control.max_mireds;
            temp_cfg.couple_color_temp_to_level_min_mireds = resolved.color_control.min_mireds;
            temp_cfg.start_up_color_temperature_mireds = resolved.color_control.color_temperature_mireds;
            color_control::feature::color_temperature::add(color_cluster, &temp_cfg);
        }
        uint16_t remaining = resolved.color_control.has_remaining_time ? resolved.color_control.remaining_time : 0;
        color_control::attribute::create_remaining_time(color_cluster, remaining);
    }
    return endpoint;
}

void after_endpoint_created(const generated_config::endpoint_raw &config, endpoint_t *endpoint)
{
    if (!endpoint || !supports_endpoint(config)) {
        return;
    }
    endpoint_config resolved = resolve_config(config);
    uint16_t endpoint_id = endpoint::get_id(endpoint);
    if (extended_color_light_endpoint_id == chip::kInvalidEndpointId) {
        extended_color_light_endpoint_id = endpoint_id;
    }
    if (resolved.color_control.enabled && resolved.color_control.has_color_temperature) {
        esp_matter_attr_val_t val = esp_matter_uint16(resolved.color_control.color_temperature_mireds);
        attribute::update(endpoint_id, chip::app::Clusters::ColorControl::Id, chip::app::Clusters::ColorControl::Attributes::ColorTemperatureMireds::Id, &val);
    }
    if (resolved.on_off.enabled) {
        esp_matter_attr_val_t val = esp_matter_bool(resolved.on_off.on);
        attribute::update(endpoint_id, chip::app::Clusters::OnOff::Id, chip::app::Clusters::OnOff::Attributes::OnOff::Id, &val);
    }
    if (resolved.level_control.enabled) {
        esp_matter_attr_val_t val = esp_matter_uint8(resolved.level_control.current_level);
        attribute::update(endpoint_id, chip::app::Clusters::LevelControl::Id, chip::app::Clusters::LevelControl::Attributes::CurrentLevel::Id, &val);
    }
}

void apply_post_stack_start()
{
}

const DeviceModule kModule = {
    .name = "extended_color_light",
    .init_drivers = init_drivers,
    .supports_endpoint = supports_endpoint,
    .create_endpoint = create_endpoint,
    .after_endpoint_created = after_endpoint_created,
    .apply_post_stack_start = apply_post_stack_start,
    .attribute_update = nullptr,
    .perform_identification = nullptr,
};

}
