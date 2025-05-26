#include "app_priv.h"
#include "common_macros.h"

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

// Using namespaces for convenience
using namespace chip::app::Clusters;
using namespace esp_matter;
using namespace esp_matter::endpoint;
using namespace esp_matter::attribute;

// Logging Tags
static const char *TAG = "APP_MAIN";

// Global endpoint ID for the light
uint16_t light_endpoint_id = chip::kInvalidEndpointId;

// --- Forward Declarations ---
esp_err_t app_attribute_update_cb(callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                  uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data);
esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                uint8_t effect_variant, void *priv_data);
void app_event_cb(const chip::DeviceLayer::ChipDeviceEvent *event, intptr_t arg);

// --- Main Application ---
extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Starting Matter Light Application (Factory Provider Mode)");
    esp_err_t err_esp = ESP_OK;

    // 1. Initialize NVS
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

    // 3. Create Matter Node
    ESP_LOGI(TAG, "Creating Matter node...");
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb, light_handle);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));
    ESP_LOGI(TAG, "Matter node created.");

    // 4. Create Light Endpoint
    ESP_LOGI(TAG, "Creating Light endpoint...");
    esp_matter::endpoint::extended_color_light::config_t light_cfg;
    light_cfg.on_off.on_off = DEFAULT_POWER;
    light_cfg.level_control.current_level = DEFAULT_BRIGHTNESS;

    // Configuración del Cluster ColorControl
    light_cfg.color_control.color_mode = static_cast<uint8_t>(chip::app::Clusters::ColorControl::ColorMode::kColorTemperature);
    light_cfg.color_control.enhanced_color_mode = static_cast<uint8_t>(chip::app::Clusters::ColorControl::ColorMode::kColorTemperature);

    // Habilitar las features deseadas usando el mapa de bits 'feature_flags'
    light_cfg.color_control.feature_flags =
        static_cast<uint32_t>(chip::app::Clusters::ColorControl::Feature::kHueAndSaturation) |
        static_cast<uint32_t>(chip::app::Clusters::ColorControl::Feature::kEnhancedHue) |
        static_cast<uint32_t>(chip::app::Clusters::ColorControl::Feature::kColorLoop) |
        static_cast<uint32_t>(chip::app::Clusters::ColorControl::Feature::kXy) |
        static_cast<uint32_t>(chip::app::Clusters::ColorControl::Feature::kColorTemperature);

    light_cfg.color_control.features.color_temperature.color_temp_physical_min_mireds = DEFAULT_COLOR_TEMP_PHYSICAL_MIN_MIREDS;
    light_cfg.color_control.features.color_temperature.color_temp_physical_max_mireds = DEFAULT_COLOR_TEMP_PHYSICAL_MAX_MIREDS;

    endpoint_t *endpoint = esp_matter::endpoint::extended_color_light::create(node, &light_cfg, ENDPOINT_FLAG_NONE, light_handle);
    ABORT_APP_ON_FAILURE(endpoint != nullptr, ESP_LOGE(TAG, "Failed to create Light endpoint"));
    light_endpoint_id = endpoint::get_id(endpoint);
    ABORT_APP_ON_FAILURE(light_endpoint_id != chip::kInvalidEndpointId, ESP_LOGE(TAG, "Error getting created endpoint ID"));
    ESP_LOGI(TAG, "Light endpoint created with ID: %u", light_endpoint_id);

    // 5. Set deferred persistence
    if (auto color_cluster = cluster::get(endpoint, ColorControl::Id))
    {
        constexpr chip::AttributeId color_attrs[] = {ColorControl::Attributes::CurrentX::Id, ColorControl::Attributes::CurrentY::Id, ColorControl::Attributes::ColorTemperatureMireds::Id};
        for (auto id : color_attrs)
        {
            if (auto attr = attribute::get(color_cluster, id))
            {
                attribute::set_deferred_persistence(attr);
            }
        }
        ESP_LOGI(TAG, "Deferred persistence configured for ColorControl attributes.");
    }

    // 6. Configure OpenThread
    ESP_LOGI(TAG, "Configuring OpenThread...");
    esp_openthread_platform_config_t config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&config);
    ESP_LOGI(TAG, "OpenThread configured.");

    // 7. Start the Matter Stack
    ESP_LOGI(TAG, "Starting Matter stack...");
    err_esp = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err_esp == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter stack: %s", esp_err_to_name(err_esp)));

    ESP_LOGI(TAG, "Matter stack started successfully.");
    ESP_LOGI(TAG, "HEAP After Matter Start: free=%zu, min=%zu",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

    // 8. Set initial driver state
    ESP_LOGI(TAG, "Setting driver defaults...");
    if (light_endpoint_id != chip::kInvalidEndpointId)
    {
        app_driver_light_set_defaults(light_endpoint_id);
        ESP_LOGI(TAG, "Driver defaults set for endpoint %u.", light_endpoint_id);
    }
    else
    {
        ESP_LOGE(TAG, "Cannot set driver defaults, invalid endpoint ID!");
    }

    // 9. Log device configuration ONLY
    ESP_LOGI(TAG, "Device ready. Logging configuration...");
    chip::DeviceLayer::ConfigurationMgr().LogDeviceConfig();
    // generate_and_print_qr_code(); // ELIMINADO

    ESP_LOGI(TAG, "Setup complete. Entering main loop.");
    ESP_LOGI(TAG, "*** Commissioning codes must be obtained from the mfg_tool output (.csv file) ***");

    // 10. Main loop
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(MAIN_LOOP_DELAY_MS));
    }
}

// --- Callback Implementations ---

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
        else
        {
            if (endpoint_id == light_endpoint_id)
            {
                ESP_LOGE(TAG, "Attribute update callback missing driver handle for Light endpoint (priv_data is NULL)");
            }
        }
    }
    return err;
}

esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback received: EP=%u, Type=%d, EffectId=0x%x, Variant=0x%x",
             endpoint_id, type, effect_id, effect_variant);

    // Solo actuar sobre el endpoint de la luz principal si es necesario diferenciar
    // if (endpoint_id == light_endpoint_id) { // O si es un endpoint raíz y aplica a todos
    app_driver_handle_t driver_handle = (app_driver_handle_t)priv_data;
    if (driver_handle)
    {
        app_driver_perform_identification(driver_handle, type, effect_id);
    }
    else
    {
        ESP_LOGE(TAG, "Identify CB: Driver handle (priv_data) is NULL for endpoint %u", endpoint_id);
    }
    // } else {
    //    ESP_LOGI(TAG, "Identification request for unhandled endpoint_id: %u", endpoint_id);
    // }
    return ESP_OK;
}

void app_event_cb(const chip::DeviceLayer::ChipDeviceEvent *event, intptr_t arg)
{
    if (event)
    {
        ESP_LOGI(TAG, "Matter stack event received: Type=%" PRIu32, static_cast<uint32_t>(event->Type));
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
        case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStarted:
            ESP_LOGI(TAG, "Commissioning session started");
            break;
        case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStopped:
            ESP_LOGI(TAG, "Commissioning session stopped");
            break;
        case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
            ESP_LOGI(TAG, "Commissioning window opened");
            break;
        case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
            ESP_LOGI(TAG, "Commissioning window closed");
            break;
        default:
            ESP_LOGD(TAG, "Unhandled Matter stack event type: %" PRIu32, static_cast<uint32_t>(event->Type));
            break;
        }
    }
    else
    {
        ESP_LOGW(TAG, "Null event received in app_event_cb");
    }
}