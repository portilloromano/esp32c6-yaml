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

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>

#include <platform/ESP32/OpenthreadLauncher.h>
#include <platform/CHIPDeviceLayer.h>

// Namespaces to simplify code
using namespace chip::app::Clusters;
using namespace esp_matter;
using namespace esp_matter::endpoint;
using namespace esp_matter::attribute;

// Log tag
static const char *TAG = "APP_MAIN";

// Global endpoint ID
uint16_t light_endpoint_id = chip::kInvalidEndpointId;

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
    app_driver_handle_t light_handle = app_driver_light_init();
    app_driver_button_init();
    ESP_LOGI(TAG, "Application drivers initialized.");

    // 3. Create the Matter node
    ESP_LOGI(TAG, "Creating Matter node...");
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb, light_handle);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));
    ESP_LOGI(TAG, "Matter node created.");

    // 4. Create endpoint from generated config
    ESP_LOGI(TAG, "Creating endpoint from generated configuration...");
    ABORT_APP_ON_FAILURE(generated_config::num_endpoints > 0, ESP_LOGE(TAG, "No endpoints defined in config.yaml"));
    const auto& ep_config = generated_config::endpoints[0];
    
    endpoint_t *endpoint = nullptr;
    if (strcmp(ep_config.device_type, "extended_color_light") == 0) {
        ESP_LOGI(TAG, "Creating extended_color_light endpoint");
        extended_color_light::config_t light_cfg;
        light_cfg.on_off.on_off = DEFAULT_POWER;
        light_cfg.level_control.current_level = DEFAULT_BRIGHTNESS;
        light_cfg.color_control.color_mode = static_cast<uint8_t>(ColorControl::ColorMode::kColorTemperature);
        light_cfg.color_control.enhanced_color_mode = static_cast<uint8_t>(ColorControl::ColorMode::kColorTemperature);
        endpoint = extended_color_light::create(node, &light_cfg, ENDPOINT_FLAG_NONE, light_handle);
    } else {
        ESP_LOGE(TAG, "This simplified version only supports extended_color_light. Found: %s", ep_config.device_type);
        abort();
    }

    ABORT_APP_ON_FAILURE(endpoint != nullptr, ESP_LOGE(TAG, "Failed to create endpoint"));
    light_endpoint_id = endpoint::get_id(endpoint);
    ESP_LOGI(TAG, "Endpoint created with ID: %u", light_endpoint_id);

    // 5. Configure OpenThread
    ESP_LOGI(TAG, "Configuring OpenThread...");
    esp_openthread_platform_config_t config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&config);
    ESP_LOGI(TAG, "OpenThread configured.");

    // 6. Start the Matter stack
    ESP_LOGI(TAG, "Starting Matter stack...");
    err_esp = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err_esp == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter stack: %s", esp_err_to_name(err_esp)));

    ESP_LOGI(TAG, "Matter stack started successfully.");

    // 7. Set driver defaults
    ESP_LOGI(TAG, "Setting driver defaults...");
    if (light_endpoint_id != chip::kInvalidEndpointId)
    {
        app_driver_light_set_defaults(light_endpoint_id);
        ESP_LOGI(TAG, "Driver defaults set for endpoint %u.", light_endpoint_id);
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
