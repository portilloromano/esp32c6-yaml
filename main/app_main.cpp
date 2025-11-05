#include "common_macros.h"
#include "generated_config.h"
#include "device_modules/device_module.h"
#include "device_modules/light/light_module.h"
#include "device_modules/switch/switch_module.h"
#include "device_modules/common/button_module.h"

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_matter.h>
#include <esp_matter_core.h>
#include <esp_matter_endpoint.h>
#include <esp_matter_cluster.h>
#include <esp_matter_data_model.h>
#include <esp_matter_providers.h>
#include <app-common/zap-generated/cluster-objects.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>

#include <platform/CHIPDeviceLayer.h>
#include <platform/ESP32/ESP32Config.h>
#include <platform/ESP32/ESP32FactoryDataProvider.h>
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/ESP32/OpenthreadLauncher.h>
#include <esp_openthread_types.h>
#endif

#include <cctype>
#include <cstring>

using namespace chip::app::Clusters;
using namespace esp_matter;
using namespace esp_matter::endpoint;

// Log tag
static const char *TAG = "APP_MAIN";
constexpr uint32_t kMainLoopDelayMs = 10000;

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
const DeviceModule *const kAvailableModules[] = {
    &device_modules::light::kModule,
    &device_modules::switch_module::kModule,
};

constexpr size_t kAvailableModuleCount = sizeof(kAvailableModules) / sizeof(kAvailableModules[0]);
bool g_module_enabled[kAvailableModuleCount] = {};
app_driver_handle_t g_module_handles[kAvailableModuleCount] = {};

void detect_enabled_modules()
{
    for (size_t idx = 0; idx < kAvailableModuleCount; ++idx) {
        g_module_enabled[idx] = false;
        g_module_handles[idx] = nullptr;
    }
    for (size_t ep_idx = 0; ep_idx < generated_config::num_endpoints; ++ep_idx) {
        const auto &ep = generated_config::endpoints[ep_idx];
        for (size_t mod_idx = 0; mod_idx < kAvailableModuleCount; ++mod_idx) {
            if (g_module_enabled[mod_idx]) {
                continue;
            }
            const DeviceModule *module = kAvailableModules[mod_idx];
            if (module->supports_endpoint && module->supports_endpoint(ep)) {
                g_module_enabled[mod_idx] = true;
                break;
            }
        }
    }
}

bool module_is_enabled(const DeviceModule *module)
{
    for (size_t idx = 0; idx < kAvailableModuleCount; ++idx) {
        if (kAvailableModules[idx] == module) {
            return g_module_enabled[idx];
        }
    }
    return false;
}

const generated_config::endpoint_raw *find_endpoint_config(uint16_t endpoint_id)
{
    for (size_t idx = 0; idx < generated_config::num_endpoints; ++idx) {
        const auto &config = generated_config::endpoints[idx];
        if (config.id == endpoint_id) {
            return &config;
        }
    }
    return nullptr;
}

const DeviceModule *find_module_for_endpoint(const generated_config::endpoint_raw &config, size_t *out_index)
{
    for (size_t idx = 0; idx < kAvailableModuleCount; ++idx) {
        if (!g_module_enabled[idx]) {
            continue;
        }
        const DeviceModule *module = kAvailableModules[idx];
        if (module->supports_endpoint && module->supports_endpoint(config)) {
            if (out_index) {
                *out_index = idx;
            }
            return module;
        }
    }
    return nullptr;
}

} // namespace

// --- Forward declarations of callbacks ---
esp_err_t app_attribute_update_cb(esp_matter::attribute::callback_type_t type,
                                  uint16_t endpoint_id,
                                  uint32_t cluster_id,
                                  uint32_t attribute_id,
                                  esp_matter_attr_val_t *val,
                                  void *priv_data);
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
    detect_enabled_modules();
    size_t active_module_count = 0;
    for (size_t idx = 0; idx < kAvailableModuleCount; ++idx) {
        if (g_module_enabled[idx]) {
            ++active_module_count;
        }
    }
    if (active_module_count == 0) {
        ESP_LOGW(TAG, "No device modules selected by configuration.");
    }

    app_driver_handle_t primary_driver_handle = nullptr;
    for (size_t idx = 0; idx < kAvailableModuleCount; ++idx) {
        if (!g_module_enabled[idx]) {
            continue;
        }
        const DeviceModule *module = kAvailableModules[idx];
        if (module && module->init_drivers) {
            app_driver_handle_t handle = module->init_drivers();
            g_module_handles[idx] = handle;
            if (!primary_driver_handle && handle) {
                primary_driver_handle = handle;
            }
        }
    }

    if (BUTTON_COUNT > 0) {
        app_driver_handle_t button_handle = device_modules::button::init();
        if (!primary_driver_handle && button_handle) {
            primary_driver_handle = button_handle;
        }
    } else {
        ESP_LOGI(TAG, "Button module disabled by configuration.");
    }
    ESP_LOGI(TAG, "Application drivers initialized.");

#if CONFIG_CUSTOM_DEVICE_INSTANCE_INFO_PROVIDER
    esp_matter::set_custom_device_instance_info_provider(&s_hyphen_friendly_factory_data_provider);
#endif

    // 3. Create the Matter node
    ESP_LOGI(TAG, "Creating Matter node...");
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb, primary_driver_handle);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));
    ESP_LOGI(TAG, "Matter node created.");

    // 4. Create endpoints from generated config
    ESP_LOGI(TAG, "Creating endpoints from generated configuration...");
    ABORT_APP_ON_FAILURE(generated_config::num_endpoints > 0, ESP_LOGE(TAG, "No endpoints defined in config.yaml"));

    for (int i = 0; i < generated_config::num_endpoints; ++i) {
        const auto &ep_config = generated_config::endpoints[i];
        ESP_LOGI(TAG, "Creating endpoint %d: type='%s'", ep_config.id, ep_config.device_type);

        size_t module_index = 0;
        const DeviceModule *module = find_module_for_endpoint(ep_config, &module_index);
        if (!module || !module->create_endpoint) {
            ESP_LOGE(TAG, "Unsupported endpoint device type '%s' in config.yaml", ep_config.device_type);
            continue;
        }
        endpoint_t *endpoint = module->create_endpoint(ep_config, node);
        if (endpoint == nullptr) {
            ESP_LOGE(TAG, "Failed to create endpoint of type %s", ep_config.device_type);
            continue;
        }
        if (module->after_endpoint_created) {
            module->after_endpoint_created(ep_config, endpoint);
        }

        const uint16_t endpoint_id = endpoint::get_id(endpoint);

        ESP_LOGI(TAG, "Endpoint %d created with ID: %u", ep_config.id, endpoint_id);
    }

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    ESP_LOGI(TAG, "Configuring OpenThread platform...");
    esp_openthread_platform_config_t ot_config = {
        .radio_config = {
            .radio_mode = RADIO_MODE_NATIVE,
        },
        .host_config = {
            .host_connection_mode = HOST_CONNECTION_MODE_NONE,
        },
        .port_config = {
            .storage_partition_name = "nvs",
            .netif_queue_size = 10,
            .task_queue_size = 10,
        },
    };
    set_openthread_platform_config(&ot_config);
#if APP_NETWORK_CONNECTIVITY_THREAD
    ESP_LOGI(TAG, "Thread connectivity enabled via YAML configuration.");
#else
    ESP_LOGI(TAG, "Thread support built in but disabled in YAML; stack will remain idle.");
#endif
#endif // CHIP_DEVICE_CONFIG_ENABLE_THREAD
    
    // 6. Start the Matter stack
    ESP_LOGI(TAG, "Starting Matter stack...");
    err_esp = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err_esp == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter stack: %s", esp_err_to_name(err_esp)));

    ESP_LOGI(TAG, "Matter stack started successfully.");

    // 7. Allow modules to apply post-start defaults
    ESP_LOGI(TAG, "Applying post-start actions for active modules...");
    for (size_t idx = 0; idx < kAvailableModuleCount; ++idx) {
        if (!g_module_enabled[idx]) {
            continue;
        }
        const DeviceModule *module = kAvailableModules[idx];
        if (module && module->apply_post_stack_start) {
            module->apply_post_stack_start();
        }
    }

    // 8. Log device configuration
    ESP_LOGI(TAG, "Device ready. Logging configuration...");
    chip::DeviceLayer::ConfigurationMgr().LogDeviceConfig();

    ESP_LOGI(TAG, "Setup complete. Entering main loop.");

    // 9. Main loop (FreeRTOS task)
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(kMainLoopDelayMs));
    }
}

// --- Callback Implementations (unchanged) ---

esp_err_t app_attribute_update_cb(esp_matter::attribute::callback_type_t type,
                                  uint16_t endpoint_id,
                                  uint32_t cluster_id,
                                  uint32_t attribute_id,
                                  esp_matter_attr_val_t *val,
                                  void * /*priv_data*/)
{
    if (type != esp_matter::attribute::PRE_UPDATE) {
        return ESP_OK;
    }

    const auto *config = find_endpoint_config(endpoint_id);
    if (!config) {
        return ESP_OK;
    }

    size_t module_index = 0;
    const DeviceModule *module = find_module_for_endpoint(*config, &module_index);
    if (!module || !module->attribute_update) {
        return ESP_OK;
    }

    app_driver_handle_t driver_handle = g_module_handles[module_index];
    return module->attribute_update(driver_handle, endpoint_id, cluster_id, attribute_id, val);
}

esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                uint8_t effect_variant, void * /*priv_data*/)
{
    const auto *config = find_endpoint_config(endpoint_id);
    if (!config) {
        return ESP_OK;
    }

    size_t module_index = 0;
    const DeviceModule *module = find_module_for_endpoint(*config, &module_index);
    if (module && module->perform_identification) {
        app_driver_handle_t driver_handle = g_module_handles[module_index];
        module->perform_identification(driver_handle, type, effect_id);
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
