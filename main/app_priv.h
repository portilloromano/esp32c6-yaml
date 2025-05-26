#pragma once
#include <esp_err.h>
#include <esp_matter.h>
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
#define CONSECUTIVE_PRESS_TIMEOUT_MS 2000
#define IDENTIFY_TRIGGER_COUNT 5
#define IDENTIFY_TIME_S 10

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
#define DEFAULT_COLOR_TEMP_PHYSICAL_MIN_MIREDS 153 // Ejemplo: ~6536K (frío)
#define DEFAULT_COLOR_TEMP_PHYSICAL_MAX_MIREDS 500 // Ejemplo:  2000K (cálido)

// Definición de los pasos para el parpadeo de identificación
#define IDENTIFY_BLINK_ON_TIME_MS 250
#define IDENTIFY_BLINK_OFF_TIME_MS 250

// Configuración OpenThread
#define ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG() { \
    .radio_mode = RADIO_MODE_NATIVE,            \
}
#define ESP_OPENTHREAD_DEFAULT_HOST_CONFIG() {         \
    .host_connection_mode = HOST_CONNECTION_MODE_NONE, \
}
#define ESP_OPENTHREAD_DEFAULT_PORT_CONFIG() { \
    .storage_partition_name = "nvs",           \
    .netif_queue_size = 10,                    \
    .task_queue_size = 10,                     \
}

typedef void *app_driver_handle_t;

/**
 * @brief Inicializa el driver del LED.
 *
 * Configura los recursos necesarios para controlar el LED (ej. GPIO, RMT para WS2812).
 *
 * @return app_driver_handle_t Handle opaco al driver del LED inicializado.
 * Retorna NULL si la inicialización falla.
 */
app_driver_handle_t app_driver_light_init(void);

/**
 * @brief Inicializa el driver del botón.
 *
 * Configura el GPIO para el botón y registra los callbacks para detectar pulsaciones
 * (cortas y largas).
 *
 * @return app_driver_handle_t Handle opaco al driver del botón inicializado.
 * Retorna NULL si la inicialización falla.
 */
app_driver_handle_t app_driver_button_init(void);

/**
 * @brief Actualiza un atributo del driver basado en un valor de Matter.
 *
 * @param driver_handle Handle al driver del dispositivo (ej. LED, botón).
 * @param endpoint_id ID del endpoint al que pertenece el atributo.
 * @param cluster_id ID del clúster al que pertenece el atributo.
 * @param attribute_id ID del atributo que se va a actualizar.
 * @param val Puntero a la estructura esp_matter_attr_val_t que contiene el nuevo valor.
 * @return esp_err_t ESP_OK si la actualización fue exitosa, o un código de error en caso contrario.
 */
esp_err_t app_driver_attribute_update(app_driver_handle_t driver_handle, uint16_t endpoint_id, uint32_t cluster_id,
                                      uint32_t attribute_id, esp_matter_attr_val_t *val);

/**
 * @brief Establece los valores por defecto del driver de la luz para un endpoint específico.
 *
 * Lee los valores iniciales de los atributos Matter (como brillo, color, encendido/apagado)
 * desde el endpoint y los aplica al hardware del LED.
 *
 * @param endpoint_id ID del endpoint para el cual se establecerán los valores por defecto del driver.
 * @return esp_err_t ESP_OK si los valores por defecto se aplicaron correctamente,
 * o un código de error en caso contrario.
 */
esp_err_t app_driver_light_set_defaults(uint16_t endpoint_id);

/**
 * @brief Callback invocado por el stack de ESP-Matter para eventos de identificación.
 *
 * Esta función es llamada cuando el dispositivo recibe un comando 'Identify'
 * o cuando el proceso de identificación debe detenerse.
 *
 * @param type Tipo de evento de identificación (START, STOP, EFFECT).
 * @param endpoint_id ID del endpoint que está siendo identificado.
 * @param effect_id Identificador del efecto solicitado (ej. parpadeo).
 * @param effect_variant Variante del efecto (ej. color por defecto).
 * @param priv_data Puntero a datos privados pasados durante la creación del nodo/endpoint (usualmente el handle del driver).
 * @return esp_err_t ESP_OK si el manejo del callback fue exitoso.
 */
esp_err_t app_identification_cb(esp_matter::identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id, uint8_t effect_variant, void *priv_data);

/**
 * @brief Callback invocado por el stack de ESP-Matter para eventos generales del dispositivo.
 *
 * Maneja eventos del ciclo de vida del dispositivo Matter, como la finalización de la puesta en marcha (commissioning),
 * conexión a la red, etc.
 *
 * @param event Puntero a la estructura del evento del dispositivo (ChipDeviceEvent).
 * @param arg Argumento opcional pasado durante el registro del callback (no usado actualmente en este ejemplo).
 */
void app_event_cb(const chip::DeviceLayer::ChipDeviceEvent *event, intptr_t arg);

/**
 * @brief Callback invocado por el stack de ESP-Matter antes o después de la actualización de un atributo.
 *
 * Permite a la aplicación reaccionar a los cambios en los atributos de Matter,
 * usualmente para actualizar el estado del hardware correspondiente.
 *
 * @param type Tipo de callback de atributo (PRE_UPDATE, POST_UPDATE).
 * @param endpoint_id ID del endpoint al que pertenece el atributo.
 * @param cluster_id ID del clúster al que pertenece el atributo.
 * @param attribute_id ID del atributo que se está actualizando.
 * @param val Puntero a esp_matter_attr_val_t con el valor del atributo (nuevo valor en PRE_UPDATE).
 * @param priv_data Puntero a datos privados pasados durante la creación del nodo/endpoint (usualmente el handle del driver).
 * @return esp_err_t ESP_OK si el manejo del callback fue exitoso, especialmente en PRE_UPDATE para permitir la actualización.
 */
esp_err_t app_attribute_update_cb(esp_matter::attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data);

/**
 * @brief Performs the identification effect on the light.
 *
 * @param driver_handle Handle to the light driver.
 * @param type Type of identification callback (start/stop).
 * @param effect_id The effect to be performed (e.g., blink).
 */
void app_driver_perform_identification(app_driver_handle_t driver_handle, esp_matter::identification::callback_type_t type, uint8_t effect_id);