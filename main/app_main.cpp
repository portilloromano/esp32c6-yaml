#include "app_priv.h"
#include "common_macros.h"
#include "generated_config.h"
#include <string.h>

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

// Namespaces para simplificar el código
using namespace chip::app::Clusters;
using namespace esp_matter;
using namespace esp_matter::endpoint;
using namespace esp_matter::attribute;

// Etiqueta de logs
static const char *TAG = "APP_MAIN";

// ID global del endpoint de la luz
uint16_t light_endpoint_id = chip::kInvalidEndpointId;

// --- Declaraciones adelantadas de callbacks ---
esp_err_t app_attribute_update_cb(callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                  uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data);
esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                uint8_t effect_variant, void *priv_data);
void app_event_cb(const chip::DeviceLayer::ChipDeviceEvent *event, intptr_t arg);

// --- Función principal ---
extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Starting Matter Light Application (Factory Provider Mode)");
    esp_err_t err_esp = ESP_OK;

    // 1. Inicializar NVS (almacenamiento no volátil)
    err_esp = nvs_flash_init();
    if (err_esp == ESP_ERR_NVS_NO_FREE_PAGES || err_esp == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "NVS partition needs erase, erasing...");
        ABORT_APP_ON_FAILURE(nvs_flash_erase() == ESP_OK, ESP_LOGE(TAG, "Failed to erase NVS!"));
        err_esp = nvs_flash_init();
    }
    ABORT_APP_ON_FAILURE(err_esp == ESP_OK, ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(err_esp)));
    ESP_LOGI(TAG, "NVS Initialized.");

    // 2. Inicializar drivers de hardware (LED y botón)
    ESP_LOGI(TAG, "Initializing application drivers...");
    app_driver_handle_t light_handle = app_driver_light_init();
    app_driver_button_init();
    ESP_LOGI(TAG, "Application drivers initialized.");

    // 3. Crear el nodo Matter
    ESP_LOGI(TAG, "Creating Matter node...");
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb, light_handle);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));
    ESP_LOGI(TAG, "Matter node created.");

    // 4. Create endpoints and clusters from generated config
    ESP_LOGI(TAG, "Creating endpoints from generated configuration...");
    for (int i = 0; i < generated_config::num_endpoints; ++i) {
        const auto& ep_config = generated_config::endpoints[i];
        ESP_LOGI(TAG, "Creating endpoint id: %d, type: %s", ep_config.id, ep_config.device_type);

        endpoint_t *endpoint = nullptr;
        if (strcmp(ep_config.device_type, "light") == 0) {
            esp_matter::endpoint::light::config_t light_cfg;
            endpoint = esp_matter::endpoint::light::create(node, &light_cfg, ENDPOINT_FLAG_NONE, light_handle);
            ABORT_APP_ON_FAILURE(endpoint != nullptr, ESP_LOGE(TAG, "Failed to create Light endpoint"));

            for (int j = 0; j < ep_config.num_clusters; ++j) {
                const auto& cluster_config = ep_config.clusters[j];
                ESP_LOGI(TAG, "Creating cluster: %s", cluster_config.name);
                if (strcmp(cluster_config.name, "on_off") == 0) {
                    on_off::config_t on_off_cfg;
                    on_off_cfg.on_off = DEFAULT_POWER;
                    cluster::on_off::create(endpoint, &on_off_cfg, CLUSTER_FLAG_SERVER);
                } else if (strcmp(cluster_config.name, "level_control") == 0) {
                    level_control::config_t level_control_cfg;
                    level_control_cfg.current_level = DEFAULT_BRIGHTNESS;
                    cluster::level_control::create(endpoint, &level_control_cfg, CLUSTER_FLAG_SERVER);
                } else if (strcmp(cluster_config.name, "color_control") == 0) {
                    color_control::config_t color_control_cfg;
                    color_control_cfg.color_mode = static_cast<uint8_t>(ColorControl::ColorMode::kColorTemperature);
                    color_control_cfg.enhanced_color_mode = static_cast<uint8_t>(ColorControl::ColorMode::kColorTemperature);
                    cluster::color_control::create(endpoint, &color_control_cfg, CLUSTER_FLAG_SERVER);
                }
            }
        }
            if (endpoint) {
                uint16_t endpoint_id = endpoint::get_id(endpoint);
                ESP_LOGI(TAG, "Endpoint created with ID: %u", endpoint_id);
                app_driver_light_set_defaults(endpoint_id);
            }
        }
    }



    // 6. Configurar OpenThread
    ESP_LOGI(TAG, "Configuring OpenThread...");
    esp_openthread_platform_config_t config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&config);
    ESP_LOGI(TAG, "OpenThread configured.");

    // 7. Iniciar la pila Matter
    ESP_LOGI(TAG, "Starting Matter stack...");
    err_esp = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err_esp == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter stack: %s", esp_err_to_name(err_esp)));

    ESP_LOGI(TAG, "Matter stack started successfully.");
    ESP_LOGI(TAG, "HEAP After Matter Start: free=%zu, min=%zu",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));



    // 9. Log de la configuración del dispositivo
    ESP_LOGI(TAG, "Device ready. Logging configuration...");
    chip::DeviceLayer::ConfigurationMgr().LogDeviceConfig();

    ESP_LOGI(TAG, "Setup complete. Entering main loop.");
    ESP_LOGI(TAG, "*** Commissioning codes must be obtained from the mfg_tool output (.csv file) ***");

    // 10. Bucle principal (tarea en FreeRTOS)
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(MAIN_LOOP_DELAY_MS));
    }
}

// --- Implementaciones de Callbacks ---

// Callback de actualización de atributos (ej. cuando un cliente cambia brillo o color)
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

// Callback de identificación (usado para parpadeos o efectos cuando el dispositivo es identificado en la red)
esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback received: EP=%u, Type=%d, EffectId=0x%x, Variant=0x%x",
             endpoint_id, type, effect_id, effect_variant);

    app_driver_handle_t driver_handle = (app_driver_handle_t)priv_data;
    if (driver_handle)
    {
        app_driver_perform_identification(driver_handle, type, effect_id);
    }
    else
    {
        ESP_LOGE(TAG, "Identify CB: Driver handle (priv_data) is NULL for endpoint %u", endpoint_id);
    }

    return ESP_OK;
}

// Callback de eventos de la pila Matter (ej. commissioning, fallos, red operativa, etc.)
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
