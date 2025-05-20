#include "app_priv.h"
#include "common_macros.h"

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_matter.h>
#include <esp_matter_core.h>
#include <esp_matter_endpoint.h>
#include <esp_matter_cluster.h>
#include <inet/InetInterface.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>

#include <esp_wifi.h>

// --- Cabeceras de CHIP/Matter ---
// Incluimos la cabecera minimalista que tu 'find' encontró.
// Esta cabecera NO define InterfaceNetworkType.
#include <platform/CHIPDeviceLayer.h>
// INCLUIMOS CHIPDeviceEvent.h donde TU GREP ENCONTRÓ InterfaceIpChangeType
#include <platform/CHIPDeviceEvent.h>

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/ESP32/OpenthreadLauncher.h>
#include <platform/ThreadStackManager.h>
#endif

// Using namespaces for convenience
using namespace chip::app::Clusters;
using namespace esp_matter;

// Logging Tags
static const char *TAG = "APP_MAIN";

// Global endpoint ID for the light
uint16_t light_endpoint_id = chip::kInvalidEndpointId;

// --- Forward Declarations ---
esp_err_t app_attribute_update_cb(esp_matter::attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                  uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data);
esp_err_t app_identification_cb(esp_matter::identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
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
    light_cfg.color_control.color_mode = static_cast<uint8_t>(chip::app::Clusters::ColorControl::ColorMode::kColorTemperature);
    light_cfg.color_control.enhanced_color_mode = static_cast<uint8_t>(chip::app::Clusters::ColorControl::ColorMode::kColorTemperature);
    endpoint_t *endpoint = esp_matter::endpoint::extended_color_light::create(node, &light_cfg, ENDPOINT_FLAG_NONE, light_handle);
    ABORT_APP_ON_FAILURE(endpoint != nullptr, ESP_LOGE(TAG, "Failed to create Light endpoint"));
    light_endpoint_id = esp_matter::endpoint::get_id(endpoint);
    ABORT_APP_ON_FAILURE(light_endpoint_id != chip::kInvalidEndpointId, ESP_LOGE(TAG, "Error getting created endpoint ID"));
    ESP_LOGI(TAG, "Light endpoint created with ID: %u", light_endpoint_id);

    // 5. Set deferred persistence
    if (auto color_cluster = esp_matter::cluster::get(endpoint, chip::app::Clusters::ColorControl::Id))
    {
        constexpr chip::AttributeId color_attrs[] = {
            chip::app::Clusters::ColorControl::Attributes::CurrentX::Id,
            chip::app::Clusters::ColorControl::Attributes::CurrentY::Id,
            chip::app::Clusters::ColorControl::Attributes::ColorTemperatureMireds::Id};
        for (auto id : color_attrs)
        {
            if (auto attr = esp_matter::attribute::get(color_cluster, id))
            {
                esp_matter::attribute::set_deferred_persistence(attr);
            }
        }
        ESP_LOGI(TAG, "Deferred persistence configured for ColorControl attributes.");
    }

    // 6. Configure OpenThread
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    ESP_LOGI(TAG, "Configuring OpenThread...");
    esp_openthread_platform_config_t config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&config);
    ESP_LOGI(TAG, "OpenThread configured.");
#else
    ESP_LOGW(TAG, "Matter over Thread is disabled in sdkconfig.");
#endif

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

    ESP_LOGI(TAG, "Setup complete. Entering main loop.");
    ESP_LOGI(TAG, "*** Commissioning codes must be obtained from the mfg_tool output (.csv file) ***");

    // 10. Main loop
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

// --- Callback Implementations ---
esp_err_t app_attribute_update_cb(esp_matter::attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                  uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    esp_err_t err = ESP_OK;
    app_driver_handle_t driver_handle = (app_driver_handle_t)priv_data;

    if (type == esp_matter::attribute::PRE_UPDATE)
    {
        if (driver_handle && endpoint_id == light_endpoint_id)
        {
            err = app_driver_attribute_update(driver_handle, endpoint_id, cluster_id, attribute_id, val);
        }
        else
        {
            ESP_LOGW(TAG, "Attribute update callback: Incorrect priv_data/driver_handle or endpoint_id mismatch. EP_ID: %u, DriverHandle: %p",
                     endpoint_id, driver_handle);
        }
    }
    return err;
}

esp_err_t app_identification_cb(esp_matter::identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback: EP=%u, Type=%d, EffectId=0x%x, Variant=0x%x",
             endpoint_id, static_cast<int>(type), effect_id, effect_variant);

    if (endpoint_id == light_endpoint_id && type == esp_matter::identification::START)
    {
        ESP_LOGI(TAG, "Identify effect START for light endpoint");
    }
    return ESP_OK;
}

void app_event_cb(const chip::DeviceLayer::ChipDeviceEvent *event, intptr_t arg)
{
    if (event)
    {
        ESP_LOGI(TAG, "Matter stack event received: Type=%" PRIu32, static_cast<uint32_t>(event->Type));
        switch (event->Type)
        {
            // ... otros cases como kCommissioningComplete, kFailSafeTimerExpired ...

        case chip::DeviceLayer::DeviceEventType::kOperationalNetworkEnabled: // <--- ESTE ES EL EVENTO
            ESP_LOGI(TAG, "Operational network enabled.");

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD // Verifica que Thread esté habilitado en la configuración
            // Se comprueba si el dispositivo está actualmente conectado a una red Thread
            if (chip::DeviceLayer::ThreadStackMgr().IsThreadAttached())
            {
                ESP_LOGI(TAG, "Thread network is attached. Attempting to disable Wi-Fi STA.");

                // Se intenta detener la interfaz Wi-Fi STA
                esp_err_t err = esp_wifi_stop();
                if (err == ESP_OK)
                {
                    ESP_LOGI(TAG, "Wi-Fi STA stopped successfully.");
                }
                else if (err == ESP_ERR_WIFI_NOT_INIT)
                {
                    ESP_LOGI(TAG, "Wi-Fi was not initialized or already stopped, no need to stop again.");
                }
                else
                {
                    ESP_LOGE(TAG, "Failed to stop Wi-Fi STA: %s (%d)", esp_err_to_name(err), err);
                }
            }
            else
            {
                ESP_LOGI(TAG, "Operational network enabled, but Thread is not the attached network. Wi-Fi state remains.");
            }
#else
            ESP_LOGI(TAG, "Thread is not enabled in this build. Wi-Fi state will remain as configured.");
#endif // CHIP_DEVICE_CONFIG_ENABLE_THREAD
            break;

            // ... otros cases como kCommissioningSessionStarted, kInterfaceIpAddressChanged, etc. ...

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