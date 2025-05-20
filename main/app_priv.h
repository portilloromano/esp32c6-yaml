#pragma once
#include <esp_err.h>    // <-- Asegúrate de incluir para esp_err_t
#include <esp_matter.h>
// #include <core/CHIPError.h> // Ya no es necesario aquí si no devuelven CHIP_ERROR
#include <platform/CHIPDeviceLayer.h> // Para ChipDeviceEvent

// Configuración del LED
#define LED_GPIO GPIO_NUM_8
#define LED_STRIP_LED_COUNT 1
#define LED_MODEL LED_MODEL_WS2812
#define LED_COLOR_FORMAT LED_STRIP_COLOR_COMPONENT_FMT_GRB

// Configuración del botón
#define BUTTON_GPIO GPIO_NUM_9
#define BUTTON_ACTIVE_LEVEL 0
#define APP_BUTTON_LONG_PRESS_TIME_MS 5000
#define APP_BUTTON_SHORT_PRESS_TIME_MS 50

/** Standard max values (used for remapping attributes) */
#define STANDARD_BRIGHTNESS 255
#define STANDARD_HUE 360
#define STANDARD_SATURATION 255
#define STANDARD_TEMPERATURE_FACTOR 1000000

/** Matter max values (used for remapping attributes) */
#define MATTER_BRIGHTNESS 254
#define MATTER_HUE 254
#define MATTER_SATURATION 254
#define MATTER_TEMPERATURE_FACTOR 1000000

/** Default attribute values used during initialization */
#define DEFAULT_POWER true
#define DEFAULT_BRIGHTNESS 64
#define DEFAULT_HUE 128
#define DEFAULT_SATURATION 254

typedef void *app_driver_handle_t;

/**
 * @brief Inicializa el driver del LED.
 */
app_driver_handle_t app_driver_light_init(void); // <- Añadido (void) para consistencia

/**
 * @brief Inicializa el botón para borrar la NVM mediante un long press.
 */
app_driver_handle_t app_driver_button_init(void); // <- Añadido (void)

/** Driver Update
 * ... (descripción sin cambios) ...
 */
esp_err_t app_driver_attribute_update(app_driver_handle_t driver_handle, uint16_t endpoint_id, uint32_t cluster_id,
                                      uint32_t attribute_id, esp_matter_attr_val_t *val);

/** Set defaults for light driver
 * ... (descripción sin cambios) ...
 */
esp_err_t app_driver_light_set_defaults(uint16_t endpoint_id);

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
// ... (defines OpenThread config sin cambios) ...
#define ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG() { .radio_mode = RADIO_MODE_NATIVE, }
#define ESP_OPENTHREAD_DEFAULT_HOST_CONFIG() { .host_connection_mode = HOST_CONNECTION_MODE_NONE, }
#define ESP_OPENTHREAD_DEFAULT_PORT_CONFIG() { .storage_partition_name = "nvs", .netif_queue_size = 10, .task_queue_size = 10, }
#endif

// --- DECLARACIONES DE CALLBACKS CORREGIDAS ---
// Devuelven esp_err_t como espera node::create
esp_err_t app_identification_cb(esp_matter::identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id, uint8_t effect_variant, void *priv_data);
// Firma correcta para event_callback_t
void app_event_cb(const chip::DeviceLayer::ChipDeviceEvent *event, intptr_t arg);
// Devuelven esp_err_t como espera node::create
esp_err_t app_attribute_update_cb(esp_matter::attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data);
// --- FIN DECLARACIONES CORREGIDAS ---