#include "app_priv.h"
#include "common_macros.h"
#include "generated_config.h"

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_matter.h>
#include <esp_matter_core.h>
#include <esp_matter_endpoint.h>
#include <esp_matter_cluster.h>
#include <esp_matter_data_model.h>
#include <esp_matter_providers.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>

#include <platform/ESP32/OpenthreadLauncher.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/ESP32/ESP32Config.h>
#include <platform/ESP32/ESP32FactoryDataProvider.h>

#include <cctype>
#include <cstring>

// Namespaces to simplify code
using namespace chip::app::Clusters;
using namespace esp_matter;
using namespace esp_matter::endpoint;
using namespace esp_matter::attribute;

// Log tag
static const char *TAG = "APP_MAIN";

// Global endpoint ID
uint16_t light_endpoint_id = chip::kInvalidEndpointId;

#if CONFIG_CUSTOM_DEVICE_INSTANCE_INFO_PROVIDER
namespace {
class HyphenFriendlyFactoryDataProvider : public chip::DeviceLayer::ESP32FactoryDataProvider
{
public:
    CHIP_ERROR GetManufacturingDate(uint16_t &year, uint8_t &month, uint8_t &day) override
    {
        char raw_date[16] = {0};
        size_t raw_len = 0;
        CHIP_ERROR read_err = chip::DeviceLayer::Internal::ESP32Config::ReadConfigValueStr(
            chip::DeviceLayer::Internal::ESP32Config::kConfigKey_ManufacturingDate, raw_date, sizeof(raw_date), raw_len);
        if (read_err != CHIP_NO_ERROR) {
            return read_err;
        }

        auto parse_digits = [](const char *data, size_t length) -> int {
            int value = 0;
            for (size_t i = 0; i < length; ++i) {
                value = (value * 10) + (data[i] - '0');
            }
            return value;
        };

        auto all_digits = [](const char *data, size_t length) -> bool {
            for (size_t i = 0; i < length; ++i) {
                if (!std::isdigit(static_cast<unsigned char>(data[i]))) {
                    return false;
                }
            }
            return true;
        };

        if (raw_len == 8 && all_digits(raw_date, raw_len)) {
            const int parsed_year = parse_digits(raw_date, 4);
            const int parsed_month = parse_digits(raw_date + 4, 2);
            const int parsed_day = parse_digits(raw_date + 6, 2);

            if (parsed_year > 0 && parsed_year <= 9999 && parsed_month >= 1 && parsed_month <= 12 && parsed_day >= 1 && parsed_day <= 31) {
                year = static_cast<uint16_t>(parsed_year);
                month = static_cast<uint8_t>(parsed_month);
                day = static_cast<uint8_t>(parsed_day);
                return CHIP_NO_ERROR;
            }
        } else if (raw_len == 10 && raw_date[4] == '-' && raw_date[7] == '-') {
            if (all_digits(raw_date, 4) && all_digits(raw_date + 5, 2) && all_digits(raw_date + 8, 2)) {
                const int parsed_year = parse_digits(raw_date, 4);
                const int parsed_month = parse_digits(raw_date + 5, 2);
                const int parsed_day = parse_digits(raw_date + 8, 2);

                if (parsed_year > 0 && parsed_year <= 9999 && parsed_month >= 1 && parsed_month <= 12 && parsed_day >= 1 && parsed_day <= 31) {
                    year = static_cast<uint16_t>(parsed_year);
                    month = static_cast<uint8_t>(parsed_month);
                    day = static_cast<uint8_t>(parsed_day);
                    return CHIP_NO_ERROR;
                }
            }
        }

        return chip::DeviceLayer::ESP32FactoryDataProvider::GetManufacturingDate(year, month, day);
    }

    CHIP_ERROR GetSetupPasscode(uint32_t &setupPasscode) override
    {
        uint32_t stored_passcode = 0;
        CHIP_ERROR err = chip::DeviceLayer::Internal::ESP32Config::ReadConfigValue(
            chip::DeviceLayer::Internal::ESP32Config::kConfigKey_SetupPinCode, stored_passcode);
        if (err != CHIP_NO_ERROR) {
            return err;
        }
        setupPasscode = stored_passcode;
        return CHIP_NO_ERROR;
    }
};

HyphenFriendlyFactoryDataProvider s_hyphen_friendly_factory_data_provider;
} // namespace
#endif // CONFIG_CUSTOM_DEVICE_INSTANCE_INFO_PROVIDER

namespace {
struct DeviceTypeInfo
{
    const char *name;
    uint32_t id;
    uint8_t version;
};

constexpr DeviceTypeInfo kDeviceTypes[] = {
    {"on_off_light", ESP_MATTER_ON_OFF_LIGHT_DEVICE_TYPE_ID, ESP_MATTER_ON_OFF_LIGHT_DEVICE_TYPE_VERSION},
    {"dimmable_light", ESP_MATTER_DIMMABLE_LIGHT_DEVICE_TYPE_ID, ESP_MATTER_DIMMABLE_LIGHT_DEVICE_TYPE_VERSION},
    {"extended_color_light", ESP_MATTER_EXTENDED_COLOR_LIGHT_DEVICE_TYPE_ID, ESP_MATTER_EXTENDED_COLOR_LIGHT_DEVICE_TYPE_VERSION},
};

bool lookup_device_type(const char *device_type, uint32_t &type_id, uint8_t &version)
{
    for (const auto &entry : kDeviceTypes) {
        if (strcmp(entry.name, device_type) == 0) {
            type_id = entry.id;
            version = entry.version;
            return true;
        }
    }
    return false;
}

template <typename ConfigT>
void apply_common_light_config(ConfigT &cfg, const generated_config::endpoint_config &ep_config)
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

void apply_level_control_config(esp_matter::endpoint::dimmable_light::config_t &cfg,
                                const generated_config::level_control_cluster_config &level_cfg)
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

void apply_color_control_config(esp_matter::endpoint::extended_color_light::config_t &cfg,
                                const generated_config::color_control_cluster_config &color_cfg)
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

bool add_descriptor_and_register(endpoint_t *endpoint,
                                 esp_matter::cluster::descriptor::config_t &descriptor_config,
                                 const char *device_type)
{
    using namespace esp_matter::cluster;

    cluster_t *descriptor_cluster = descriptor::create(endpoint, &descriptor_config, CLUSTER_FLAG_SERVER);
    if (!descriptor_cluster) {
        ESP_LOGE(TAG, "Failed to create descriptor cluster");
        return false;
    }

    uint32_t type_id = 0;
    uint8_t version = 0;
    if (!lookup_device_type(device_type, type_id, version)) {
        ESP_LOGE(TAG, "Unsupported device type in config.yaml: %s", device_type);
        return false;
    }

    if (add_device_type(endpoint, type_id, version) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register device type %s", device_type);
        return false;
    }

    return true;
}

template <typename ConfigT>
bool add_common_light_clusters(endpoint_t *endpoint,
                               ConfigT &cfg,
                               const generated_config::endpoint_config &ep_config)
{
    using namespace esp_matter::cluster;

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
                               esp_matter::endpoint::dimmable_light::config_t &cfg,
                               const generated_config::level_control_cluster_config &level_cfg)
{
    using namespace esp_matter::cluster;

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
                               esp_matter::endpoint::extended_color_light::config_t &cfg,
                               const generated_config::color_control_cluster_config &color_cfg)
{
    using namespace esp_matter::cluster;

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

endpoint_t *create_on_off_light_endpoint(const generated_config::endpoint_config &ep_config,
                                         node_t *node,
                                         void *priv_data)
{
    esp_matter::endpoint::on_off_light::config_t cfg;
    apply_common_light_config(cfg, ep_config);

    endpoint_t *endpoint = endpoint::create(node, ENDPOINT_FLAG_NONE, priv_data);
    if (!endpoint) {
        ESP_LOGE(TAG, "Failed to allocate endpoint for device type %s", ep_config.device_type);
        return nullptr;
    }

    if (!add_descriptor_and_register(endpoint, cfg.descriptor, ep_config.device_type)) {
        return nullptr;
    }

    if (!add_common_light_clusters(endpoint, cfg, ep_config)) {
        return nullptr;
    }

    return endpoint;
}

endpoint_t *create_dimmable_light_endpoint(const generated_config::endpoint_config &ep_config,
                                           node_t *node,
                                           void *priv_data)
{
    esp_matter::endpoint::dimmable_light::config_t cfg;
    apply_common_light_config(cfg, ep_config);
    apply_level_control_config(cfg, ep_config.level_control);

    endpoint_t *endpoint = endpoint::create(node, ENDPOINT_FLAG_NONE, priv_data);
    if (!endpoint) {
        ESP_LOGE(TAG, "Failed to allocate endpoint for device type %s", ep_config.device_type);
        return nullptr;
    }

    if (!add_descriptor_and_register(endpoint, cfg.descriptor, ep_config.device_type)) {
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

endpoint_t *create_extended_color_light_endpoint(const generated_config::endpoint_config &ep_config,
                                                 node_t *node,
                                                 void *priv_data)
{
    esp_matter::endpoint::extended_color_light::config_t cfg;
    apply_common_light_config(cfg, ep_config);
    apply_level_control_config(cfg, ep_config.level_control);
    apply_color_control_config(cfg, ep_config.color_control);

    endpoint_t *endpoint = endpoint::create(node, ENDPOINT_FLAG_NONE, priv_data);
    if (!endpoint) {
        ESP_LOGE(TAG, "Failed to allocate endpoint for device type %s", ep_config.device_type);
        return nullptr;
    }

    if (!add_descriptor_and_register(endpoint, cfg.descriptor, ep_config.device_type)) {
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

endpoint_t *create_endpoint_from_config(const generated_config::endpoint_config &ep_config,
                                        node_t *node,
                                        void *priv_data)
{
    if (strcmp(ep_config.device_type, "extended_color_light") == 0) {
        return create_extended_color_light_endpoint(ep_config, node, priv_data);
    }
    if (strcmp(ep_config.device_type, "dimmable_light") == 0) {
        return create_dimmable_light_endpoint(ep_config, node, priv_data);
    }
    if (strcmp(ep_config.device_type, "on_off_light") == 0) {
        return create_on_off_light_endpoint(ep_config, node, priv_data);
    }

    ESP_LOGE(TAG, "Unsupported endpoint device type '%s' in config.yaml", ep_config.device_type);
    return nullptr;
}

} // namespace

// --- Forward declarations of callbacks ---
esp_err_t app_attribute_update_cb(callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                  uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data);
esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                uint8_t effect_variant, void *priv_data);
void app_event_cb(const chip::DeviceLayer::ChipDeviceEvent *event, intptr_t arg);

// --- Main function ---
extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Starting Matter Application with device type: %s", generated_config::device_type);
    esp_err_t err_esp = ESP_OK;

    // 1. Initialize NVS (non-volatile storage)
    err_esp = nvs_flash_init();
    if (err_esp == ESP_ERR_NVS_NO_FREE_PAGES || err_esp == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "NVS partition needs erase, erasing...");
        ABORT_APP_ON_FAILURE(nvs_flash_erase() == ESP_OK, ESP_LOGE(TAG, "Failed to erase NVS!"));
        err_esp = nvs_flash_init();
    }
    ABORT_APP_ON_FAILURE(err_esp == ESP_OK, ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(err_esp)));
    ESP_LOGI(TAG, "NVS Initialized.");

    // 2. Initialize hardware drivers
    ESP_LOGI(TAG, "Initializing application drivers...");
    app_driver_handle_t driver_handle = nullptr;
    if (strcmp(generated_config::device_type, "light") == 0) {
        driver_handle = app_driver_light_init();
    } else {
        ESP_LOGW(TAG, "No driver initializer for device type: %s", generated_config::device_type);
    }
    app_driver_button_init();
    ESP_LOGI(TAG, "Application drivers initialized.");

#if CONFIG_CUSTOM_DEVICE_INSTANCE_INFO_PROVIDER
    esp_matter::set_custom_device_instance_info_provider(&s_hyphen_friendly_factory_data_provider);
#endif

    // 3. Create the Matter node
    ESP_LOGI(TAG, "Creating Matter node...");
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb, driver_handle);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));
    ESP_LOGI(TAG, "Matter node created.");

    // 4. Create endpoints from generated config
    ESP_LOGI(TAG, "Creating endpoints from generated configuration...");
    ABORT_APP_ON_FAILURE(generated_config::num_endpoints > 0, ESP_LOGE(TAG, "No endpoints defined in config.yaml"));

    for (int i = 0; i < generated_config::num_endpoints; ++i) {
        const auto &ep_config = generated_config::endpoints[i];
        ESP_LOGI(TAG, "Creating endpoint %d: type='%s'", ep_config.id, ep_config.device_type);

        endpoint_t *endpoint = create_endpoint_from_config(ep_config, node, driver_handle);
        if (endpoint == nullptr) {
            ESP_LOGE(TAG, "Failed to create endpoint of type %s", ep_config.device_type);
            continue;
        }

        const uint16_t endpoint_id = endpoint::get_id(endpoint);

        if (ep_config.on_off.enabled) {
            esp_matter_attr_val_t val = esp_matter_bool(ep_config.on_off.on);
            attribute::update(endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id, &val);
        }

        if (ep_config.level_control.enabled) {
            esp_matter_attr_val_t val = esp_matter_uint8(ep_config.level_control.current_level);
            attribute::update(endpoint_id, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id, &val);
        }

        if (ep_config.color_control.enabled) {
            esp_matter_attr_val_t val;
            if (ep_config.color_control.has_color_temperature) {
                val = esp_matter_uint16(ep_config.color_control.color_temperature_mireds);
                attribute::update(endpoint_id, ColorControl::Id, ColorControl::Attributes::ColorTemperatureMireds::Id, &val);
            }
            if (ep_config.color_control.has_current_hue) {
                val = esp_matter_uint8(ep_config.color_control.current_hue);
                attribute::update(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentHue::Id, &val);
            }
            if (ep_config.color_control.has_current_saturation) {
                val = esp_matter_uint8(ep_config.color_control.current_saturation);
                attribute::update(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentSaturation::Id, &val);
            }
        }

        if (light_endpoint_id == chip::kInvalidEndpointId) {
            light_endpoint_id = endpoint_id;
        }

        ESP_LOGI(TAG, "Endpoint %d created with ID: %u", ep_config.id, endpoint_id);
    }

#if CONFIG_APP_NETWORK_CONNECTIVITY_THREAD
    // 5. Configure OpenThread
    ESP_LOGI(TAG, "Configuring OpenThread...");
    esp_openthread_platform_config_t config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&config);
    ESP_LOGI(TAG, "OpenThread configured.");
#endif // CONFIG_APP_NETWORK_CONNECTIVITY_THREAD
    
    // 6. Start the Matter stack
    ESP_LOGI(TAG, "Starting Matter stack...");
    err_esp = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err_esp == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter stack: %s", esp_err_to_name(err_esp)));

    ESP_LOGI(TAG, "Matter stack started successfully.");

    // 7. Set driver defaults
    ESP_LOGI(TAG, "Setting driver defaults...");
    if (strcmp(generated_config::device_type, "light") == 0) {
        if (light_endpoint_id != chip::kInvalidEndpointId)
        {
            app_driver_light_set_defaults(light_endpoint_id);
            ESP_LOGI(TAG, "Driver defaults set for endpoint %u.", light_endpoint_id);
        }
    }

    // 8. Log device configuration
    ESP_LOGI(TAG, "Device ready. Logging configuration...");
    chip::DeviceLayer::ConfigurationMgr().LogDeviceConfig();

    ESP_LOGI(TAG, "Setup complete. Entering main loop.");

    // 9. Main loop (FreeRTOS task)
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(MAIN_LOOP_DELAY_MS));
    }
}

// --- Callback Implementations (unchanged) ---

esp_err_t app_attribute_update_cb(callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                  uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    esp_err_t err = ESP_OK;
    if (type == PRE_UPDATE)
    {
        app_driver_handle_t driver_handle = (app_driver_handle_t)priv_data;
        if (driver_handle)
        {
            err = app_driver_attribute_update(driver_handle, endpoint_id, cluster_id, attribute_id, val);
        }
    }
    return err;
}

esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                uint8_t effect_variant, void *priv_data)
{
    app_driver_handle_t driver_handle = (app_driver_handle_t)priv_data;
    if (driver_handle)
    {
        app_driver_perform_identification(driver_handle, type, effect_id);
    }
    return ESP_OK;
}

void app_event_cb(const chip::DeviceLayer::ChipDeviceEvent *event, intptr_t arg)
{
    if (event)
    {
        switch (event->Type)
        {
        case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
            ESP_LOGI(TAG, "Commissioning complete");
            break;
        case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
            ESP_LOGW(TAG, "Fail-safe timer expired. Commissioning failed or timed out");
            break;
        case chip::DeviceLayer::DeviceEventType::kOperationalNetworkEnabled:
            ESP_LOGI(TAG, "Operational network enabled (Thread/WiFi)");
            break;
        default:
            break;
        }
    }
}
