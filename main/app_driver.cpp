#include <app_priv.h>
#include "common_macros.h"
#include "generated_config.h" // Incluir para acceder a las configuraciones del YAML

#include <stdio.h>
#include <string.h> // Para memcpy
#include <led_indicator.h>
#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_system.h>
#include <iot_button.h>
#include "driver/gpio.h"
#include "button_gpio.h"
#include <esp_matter.h>
#include <esp_matter_cluster.h>

using namespace chip::app::Clusters;
using namespace esp_matter;

static const char *TAG = "app_driver";
extern uint16_t light_endpoint_id;

static uint8_t s_short_press_count = 0;
static TickType_t s_last_short_press_tick = 0;

// Variables to save LED state before identification
static bool s_previous_on_off_state = false;
static led_indicator_ihsv_t s_previous_hsv_state = {0, 0, 0};
static bool s_is_identifying = false;


static esp_err_t app_driver_light_set_power(led_indicator_handle_t handle, esp_matter_attr_val_t *val)
{
#if LED_STRIP_LED_COUNT > 0
    if (val->val.b) {
        return led_indicator_set_on_off(handle, true);
    } else {
        return led_indicator_set_on_off(handle, false);
    }
#else
    ESP_LOGI(TAG, "LED set power: %d (LED count is 0, visual update skipped)", val->val.b);
    return ESP_OK;
#endif
}

static esp_err_t app_driver_light_set_brightness(led_indicator_handle_t handle, esp_matter_attr_val_t *val)
{
    int value = REMAP_TO_RANGE(val->val.u8, MATTER_BRIGHTNESS, STANDARD_BRIGHTNESS);
#if LED_STRIP_LED_COUNT > 0
    return led_indicator_set_brightness(handle, value);
#else
    ESP_LOGI(TAG, "LED set brightness: %d (LED count is 0, visual update skipped)", value);
    return ESP_OK;
#endif
}

static esp_err_t app_driver_light_set_hue(led_indicator_handle_t handle, esp_matter_attr_val_t *val)
{
    int value = REMAP_TO_RANGE(val->val.u8, MATTER_HUE, STANDARD_HUE);
#if LED_STRIP_LED_COUNT > 0
    led_indicator_ihsv_t hsv;
    hsv.value = led_indicator_get_hsv(handle);
    hsv.h = value;
    return led_indicator_set_hsv(handle, hsv.value);
#else
    ESP_LOGI(TAG, "LED set hue: %d (LED count is 0, visual update skipped)", value);
    return ESP_OK;
#endif
}

static esp_err_t app_driver_light_set_saturation(led_indicator_handle_t handle, esp_matter_attr_val_t *val)
{
    int value = REMAP_TO_RANGE(val->val.u8, MATTER_SATURATION, STANDARD_SATURATION);
#if LED_STRIP_LED_COUNT > 0
    led_indicator_ihsv_t hsv;
    hsv.value = led_indicator_get_hsv(handle);
    hsv.s = value;
    return led_indicator_set_hsv(handle, hsv.value);
#else
    ESP_LOGI(TAG, "LED set saturation: %d (LED count is 0, visual update skipped)", value);
    return ESP_OK;
#endif
}

static esp_err_t app_driver_light_set_temperature(led_indicator_handle_t handle, esp_matter_attr_val_t *val)
{
    uint32_t value = REMAP_TO_RANGE_INVERSE(val->val.u16, STANDARD_TEMPERATURE_FACTOR);
#if LED_STRIP_LED_COUNT > 0
    return led_indicator_set_color_temperature(handle, value);
#else
    ESP_LOGI(TAG, "LED set temperature: %ld (LED count is 0, visual update skipped)", value);
    return ESP_OK;
#endif
}

static esp_err_t app_driver_set_default_brightness(uint16_t endpoint_id, led_indicator_handle_t handle)
{
    esp_err_t err = ESP_OK;
    esp_matter_attr_val_t val = esp_matter_invalid(NULL);
    attribute_t *attribute = attribute::get(endpoint_id, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);

    if (!attribute)
    {
        ESP_LOGE(TAG, "Failed to get attribute LevelControl::CurrentLevel (ID: 0x%04X)!", (unsigned int)LevelControl::Attributes::CurrentLevel::Id);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Attribute LevelControl::CurrentLevel found.");
    esp_err_t get_val_err = attribute::get_val(attribute, &val);
    if (get_val_err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get_val for CurrentLevel: %s", esp_err_to_name(get_val_err));
        err |= get_val_err;
    }
    else
    {
        err |= app_driver_light_set_brightness(handle, &val);
    }
    return err;
}

static esp_err_t app_driver_set_default_color(uint16_t endpoint_id, led_indicator_handle_t handle)
{
    esp_err_t err = ESP_OK;
    esp_matter_attr_val_t val = esp_matter_invalid(NULL);
    attribute_t *attribute = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::ColorMode::Id);

    if (!attribute)
    {
        ESP_LOGE(TAG, "Failed to get attribute ColorControl::ColorMode (ID: 0x%04X) for endpoint %u!", (unsigned int)ColorControl::Attributes::ColorMode::Id, endpoint_id);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Attribute ColorControl::ColorMode for endpoint %u found.", endpoint_id);
    esp_err_t get_val_err = attribute::get_val(attribute, &val);
    if (get_val_err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get_val for ColorMode: %s", esp_err_to_name(get_val_err));
        err |= get_val_err;
    }
    else
    {
        if (val.val.u8 == (uint8_t)ColorControl::ColorMode::kColorTemperature)
        {
            ESP_LOGI(TAG, "ColorMode is kColorTemperature. Getting ColorTemperatureMireds.");
            attribute_t *temp_attribute = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::ColorTemperatureMireds::Id);
            if (!temp_attribute)
            {
                ESP_LOGE(TAG, "Failed to get attribute ColorControl::ColorTemperatureMireds (ID: 0x%04X) for endpoint %u!", (unsigned int)ColorControl::Attributes::ColorTemperatureMireds::Id, endpoint_id);
                err |= ESP_FAIL; // Mark error
                return err; // Return immediately as this is critical for this path
            }
            else
            {
                ESP_LOGI(TAG, "Attribute ColorControl::ColorTemperatureMireds for endpoint %u found.", endpoint_id);
                esp_matter_attr_val_t temp_val = esp_matter_invalid(NULL);
                get_val_err = attribute::get_val(temp_attribute, &temp_val);
                if (get_val_err != ESP_OK)
                {
                    ESP_LOGE(TAG, "Failed to get_val for ColorTemperatureMireds: %s", esp_err_to_name(get_val_err));
                    err |= get_val_err;
                }
                else
                {
                    err |= app_driver_light_set_temperature(handle, &temp_val);
                }
            }
        }
        else if (val.val.u8 == (uint8_t)ColorControl::ColorMode::kCurrentHueAndCurrentSaturation)
        {
            ESP_LOGI(TAG, "ColorMode is kCurrentHueAndCurrentSaturation.");
            attribute_t *hue_attribute = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentHue::Id);
            if (!hue_attribute)
            {
                ESP_LOGE(TAG, "Failed to get attribute CurrentHue (ID: 0x%04X) for endpoint %u!", (unsigned int)ColorControl::Attributes::CurrentHue::Id, endpoint_id);
                err |= ESP_FAIL; // Mark error
                // Do not return yet, try to get Saturation as well, but err will reflect failure.
            }
            else
            {
                esp_matter_attr_val_t hue_val = esp_matter_invalid(NULL);
                get_val_err = attribute::get_val(hue_attribute, &hue_val);
                if (get_val_err != ESP_OK)
                {
                    ESP_LOGE(TAG, "Failed to get_val for CurrentHue: %s", esp_err_to_name(get_val_err));
                    err |= get_val_err;
                }
                else
                {
                    err |= app_driver_light_set_hue(handle, &hue_val);
                }
            }

            attribute_t *sat_attribute = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentSaturation::Id);
            if (!sat_attribute)
            {
                ESP_LOGE(TAG, "Failed to get attribute CurrentSaturation (ID: 0x%04X) for endpoint %u!", (unsigned int)ColorControl::Attributes::CurrentSaturation::Id, endpoint_id);
                err |= ESP_FAIL; // Mark error
            }
            else
            {
                esp_matter_attr_val_t sat_val = esp_matter_invalid(NULL);
                get_val_err = attribute::get_val(sat_attribute, &sat_val);
                if (get_val_err != ESP_OK)
                {
                    ESP_LOGE(TAG, "Failed to get_val for CurrentSaturation: %s", esp_err_to_name(get_val_err));
                    err |= get_val_err;
                }
                else
                {
                    err |= app_driver_light_set_saturation(handle, &sat_val);
                }
            }
        }
        else
        {
            ESP_LOGW(TAG, "Color mode 0x%02X not fully handled for defaults in this example", val.val.u8);
            // Consider if this should be an error or not. For now, just warning.
        }
    }
    return err;
}

static esp_err_t app_driver_set_default_power(uint16_t endpoint_id, led_indicator_handle_t handle)
{
    esp_err_t err = ESP_OK;
    esp_matter_attr_val_t val = esp_matter_invalid(NULL);
    attribute_t *attribute = attribute::get(endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id);

    if (!attribute)
    {
        ESP_LOGE(TAG, "Failed to get attribute OnOff::OnOff (ID: 0x%04X)!", (unsigned int)OnOff::Attributes::OnOff::Id);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Attribute OnOff::OnOff found.");
    esp_err_t get_val_err = attribute::get_val(attribute, &val);
    if (get_val_err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get_val for OnOff: %s", esp_err_to_name(get_val_err));
        err |= get_val_err;
    }
    else
    {
        err |= app_driver_light_set_power(handle, &val);
    }
    return err;
}

esp_err_t app_driver_attribute_update(app_driver_handle_t driver_handle, uint16_t endpoint_id, uint32_t cluster_id,
                                      uint32_t attribute_id, esp_matter_attr_val_t *val)
{
    ESP_LOGI(TAG, "Updating attribute - Cluster: 0x%" PRIx32 ", Attribute: 0x%" PRIx32 ", Value: %d",
             cluster_id, attribute_id, val->val.u8);

    esp_err_t err = ESP_OK;
    if (endpoint_id == light_endpoint_id)
    {
        led_indicator_handle_t handle = (led_indicator_handle_t)driver_handle;
        if (cluster_id == OnOff::Id)
        {
            if (attribute_id == OnOff::Attributes::OnOff::Id)
            {
                err = app_driver_light_set_power(handle, val);
            }
        }
        else if (cluster_id == LevelControl::Id)
        {
            if (attribute_id == LevelControl::Attributes::CurrentLevel::Id)
            {
                err = app_driver_light_set_brightness(handle, val);
            }
        }
        else if (cluster_id == ColorControl::Id)
        {
            if (attribute_id == ColorControl::Attributes::CurrentHue::Id)
            {
                err = app_driver_light_set_hue(handle, val);
            }
            else if (attribute_id == ColorControl::Attributes::CurrentSaturation::Id)
            {
                err = app_driver_light_set_saturation(handle, val);
            }
            else if (attribute_id == ColorControl::Attributes::ColorTemperatureMireds::Id)
            {
                err = app_driver_light_set_temperature(handle, val);
            }
        }
    }
    return err;
}

esp_err_t app_driver_light_set_defaults(uint16_t endpoint_id)
{
    ESP_LOGI(TAG, "Setting defaults for endpoint %u", endpoint_id);
    esp_err_t err = ESP_OK;
    void *priv_data = endpoint::get_priv_data(endpoint_id);
    led_indicator_handle_t handle = (led_indicator_handle_t)priv_data;

    // The handle might be NULL if LED_STRIP_LED_COUNT is 0, but the individual set functions
    // already handle this with their own #if guards.
#if LED_STRIP_LED_COUNT == 0
    ESP_LOGW(TAG, "app_driver_light_set_defaults: LED strip disabled. Proceeding without LED operations.");
#endif
    err |= app_driver_set_default_brightness(endpoint_id, handle);
    err |= app_driver_set_default_color(endpoint_id, handle);
    err |= app_driver_set_default_power(endpoint_id, handle);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error occurred while setting driver defaults for endpoint %u.", endpoint_id);
    }
    else
    {
        ESP_LOGI(TAG, "Driver defaults set successfully for endpoint %u.", endpoint_id);
    }
    return err;
}

void app_driver_perform_identification(app_driver_handle_t driver_handle, esp_matter::identification::callback_type_t type, uint8_t effect_id)
{
    ESP_LOGI(TAG, "Identify action: Type=%d, EffectID=0x%02x", (int)type, effect_id);

    // Handle identification state regardless of LED presence
    if (type == esp_matter::identification::START) {
        if (s_is_identifying) {
            ESP_LOGI(TAG, "Identify: Already identifying. Ignoring new START.");
            return;
        }
        s_is_identifying = true;
    } else if (type == esp_matter::identification::STOP && s_is_identifying) {
        // Only reset if it was actually identifying.
        // If not identifying, the log below will handle it.
        s_is_identifying = false;
    }

    // LED-specific identification actions
#if LED_STRIP_LED_COUNT > 0 && defined(LED_STRIPS_MODE) // Asegurarse de que el modo de tiras LED está habilitado
    led_indicator_handle_t handle = (led_indicator_handle_t)driver_handle;
    if (!handle) {
        ESP_LOGE(TAG, "Identify: Invalid LED strip driver handle.");
        return;
    }
    if (type == esp_matter::identification::START) {
        ESP_LOGI(TAG, "Identify: Saving current LED state before starting identification.");
        uint8_t current_brightness = led_indicator_get_brightness(handle);
        s_previous_on_off_state = (current_brightness > 0);
        s_previous_hsv_state.value = led_indicator_get_hsv(handle); // Guardar el estado HSV actual

        ESP_LOGD(TAG, "Identify: State saved. Prev OnOff: %d, Prev H: %d, S: %d, V: %d, Brightness: %d",
                 s_previous_on_off_state, s_previous_hsv_state.h, s_previous_hsv_state.s, s_previous_hsv_state.v, current_brightness);

        // Como las funciones de parpadeo no están disponibles, simplemente encendemos el LED
        // o lo ponemos a un brillo específico para indicar identificación.
        // Una implementación más avanzada requeriría una tarea de FreeRTOS para parpadear manualmente.
        ESP_LOGI(TAG, "Identify: Setting LED to full brightness for identification (no blink support).");
        esp_err_t err_set = led_indicator_set_brightness(handle, STANDARD_BRIGHTNESS);
        if (err_set != ESP_OK) {
            ESP_LOGE(TAG, "Identify: Failed to set LED brightness for identification: %s", esp_err_to_name(err_set));
        }
    } else if (type == esp_matter::identification::STOP) {
        if (s_is_identifying) // This check is redundant now, but harmless. The outer check handles the logic.
        {
            ESP_LOGI(TAG, "Identify: Stopping identification and restoring previous LED state.");

            // Restaurar el estado HSV guardado
            esp_err_t err_hsv = led_indicator_set_hsv(handle, s_previous_hsv_state.value);
            if (err_hsv != ESP_OK) {
                ESP_LOGE(TAG, "Identify: Failed to restore HSV state: %s", esp_err_to_name(err_hsv));
            }
            // Asegurar el estado on/off después de restaurar HSV, ya que set_hsv podría encender el LED si V > 0.
            // Si el estado previo era OFF, debemos apagarlo explícitamente.
            esp_err_t err_on_off = led_indicator_set_on_off(handle, s_previous_on_off_state);
            if (err_on_off != ESP_OK) {
                ESP_LOGE(TAG, "Identify: Failed to restore on/off state: %s", esp_err_to_name(err_on_off));
            }
            ESP_LOGI(TAG, "Identify: Previous LED state restoration attempted.");
        }
        else
        {
            ESP_LOGI(TAG, "Identify STOP received, but was not actively identifying with LEDs.");
        }
    }
#else // LED_STRIP_LED_COUNT == 0
    ESP_LOGI(TAG, "LED strip disabled. Visual identification skipped.");
#endif
}

#if BUTTON_COUNT > 0 // Assuming BUTTON_COUNT is defined in generated_config.h or sdkconfig
static void button_long_press_cb(void *btn_handle, void *usr_data)
{
    ESP_LOGI(TAG, "Long press detectado: borrando NVM...");
    esp_err_t ret = nvs_flash_erase();
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "NVM borrada, reinicializando...");
        ret = nvs_flash_init();
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Error al reinicializar NVM: %s", esp_err_to_name(ret));
        }
    }
    else
    {
        ESP_LOGE(TAG, "Error al borrar NVM: %s", esp_err_to_name(ret));
    }
    // Reinicia el dispositivo para aplicar los cambios
    esp_restart();
}

// Callback para la acción de single click (toque corto)
static void button_short_press_cb(void *btn_handle, void *usr_data)
{
    ESP_LOGI(TAG, "Button Short Press: Click detectado.");

    // --- Lógica de detección de multi-pulsación (fuera del bloqueo del stack de Matter) ---
    TickType_t current_tick = xTaskGetTickCount();

    // Si es la primera pulsación después de un timeout, o la primera vez, reiniciar contador a 1
    if (s_short_press_count == 0 || // Primera pulsación o timeout
        ((current_tick - s_last_short_press_tick) * portTICK_PERIOD_MS > generated_config::button::short_press_timeout_ms))
    {
        s_short_press_count = 1;
        ESP_LOGI(TAG, "Button Short Press: Conteo reiniciado a 1.");
    }
    else
    {
        s_short_press_count++;
    }
    s_last_short_press_tick = current_tick;
    ESP_LOGI(TAG, "Button Short Press: Conteo actual = %u", s_short_press_count);

    // --- Lógica de acción (dentro del bloqueo del stack de Matter) ---
    if (chip::DeviceLayer::PlatformMgr().TryLockChipStack())
    {
        // --- Acción de Toggle On/Off (se ejecuta en cada pulsación) ---
        endpoint_t *ep = endpoint::get(light_endpoint_id);
        if (ep == NULL)
        {
            ESP_LOGE(TAG, "Button CB: No se encontró el endpoint con ID: %u", light_endpoint_id);
            chip::DeviceLayer::PlatformMgr().UnlockChipStack();
            s_short_press_count = 0; // Reiniciar conteo si hay error temprano
            return;
        }

        cluster_t *onoff_cluster = cluster::get(ep, OnOff::Id);
        attribute_t *onoff_attr = NULL;
        if (onoff_cluster)
        {
            onoff_attr = attribute::get(onoff_cluster, OnOff::Attributes::OnOff::Id);
        }

        if (onoff_attr == NULL)
        {
            ESP_LOGE(TAG, "Button CB: No se encontró el atributo OnOff para EP %u", light_endpoint_id);
            // Continuar para posible acción de Identify, pero el toggle no funcionará
        }
        else
        {
            esp_matter_attr_val_t current_val = esp_matter_invalid(NULL);
            esp_err_t err_get = attribute::get_val(onoff_attr, &current_val);
            if (err_get != ESP_OK)
            {
                ESP_LOGE(TAG, "Button CB: Fallo al obtener valor del atributo OnOff: %s", esp_err_to_name(err_get));
                // Continuar para posible acción de Identify
            }
            else
            {
                bool current_state = current_val.val.b;
                bool new_state = !current_state;
                esp_matter_attr_val_t new_val_matter = esp_matter_bool(new_state);

                ESP_LOGI(TAG, "Button CB: Cambiando luz de %s a %s", current_state ? "ON" : "OFF", new_state ? "ON" : "OFF");
                esp_err_t err_update = attribute::update(light_endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id, &new_val_matter);
                if (err_update != ESP_OK)
                {
                    ESP_LOGE(TAG, "Button CB: Fallo al actualizar atributo OnOff: %s", esp_err_to_name(err_update));
                }
            }
        }

        // --- Acción de Identificación si se alcanza el conteo ---
        if (s_short_press_count >= generated_config::button::identify_trigger_count)
        {
            ESP_LOGI(TAG, "Button Short Press: %u pulsaciones alcanzadas. Activando Identify por %d segundos.", generated_config::button::identify_trigger_count, generated_config::button::identify_time_s);

            esp_matter_attr_val_t identify_time_val = esp_matter_uint16(generated_config::button::identify_time_s); // IdentifyTime es uint16_t
            // Asegúrate de que light_endpoint_id tiene el clúster Identify.
            // El endpoint raíz (0) o el endpoint específico de la luz (1 en tu caso).
            // El clúster Identify suele estar en todos los endpoints que deben identificarse.
            // Si la luz es el endpoint 1, y tiene el clúster Identify:
            esp_err_t identify_err = attribute::update(light_endpoint_id,
                                                       chip::app::Clusters::Identify::Id,                           // ID del Clúster Identify
                                                       chip::app::Clusters::Identify::Attributes::IdentifyTime::Id, // ID del Atributo IdentifyTime
                                                       &identify_time_val);
            if (identify_err == ESP_OK)
            {
                ESP_LOGI(TAG, "Button Short Press: Atributo IdentifyTime actualizado para iniciar identificación.");
            }
            else
            {
                ESP_LOGE(TAG, "Button Short Press: Fallo al actualizar atributo IdentifyTime: %s", esp_err_to_name(identify_err));
            }
            s_short_press_count = 0; // Reiniciar el contador después de activar Identify
        }
        chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    }
    else
    {
        ESP_LOGE(TAG, "Button CB: Fallo al adquirir el bloqueo del stack de Matter. Acción omitida.");
        // Si no se puede obtener el bloqueo, es mejor reiniciar el contador para evitar
        // una activación de Identify inesperada en la siguiente pulsación si el bloqueo sí se obtiene.
        s_short_press_count = 0;
    }
}

app_driver_handle_t app_driver_button_init()
{
    button_config_t btn_config = {
        .long_press_time = generated_config::button::long_press_time_ms,
        .short_press_time = 50, // APP_BUTTON_SHORT_PRESS_TIME_MS no se usa directamente en button_config_t, 50ms es un valor común
    };

    // Configurar el driver GPIO usando button_gpio_config_t
    button_gpio_config_t gpio_config = {
        .gpio_num = (gpio_num_t)generated_config::button::gpio,
        .active_level = (uint8_t)generated_config::button::active_level,
        .disable_pull = false,      // Puedes deshabilitar pull-up/down si es necesario
    };

    button_handle_t btn_handle = NULL;
    // Crear el dispositivo botón GPIO usando iot_button_new_gpio_device
    esp_err_t ret = iot_button_new_gpio_device(&btn_config, &gpio_config, &btn_handle); // Usar iot_button_new_gpio_device y pasar ambas configuraciones
    if (ret != ESP_OK || btn_handle == NULL)
    {
        ESP_LOGE(TAG, "Button create failed: %s", esp_err_to_name(ret));
        return NULL;
    }

    // Configurar los argumentos para el long press
    button_event_args_t long_press_args;
    long_press_args.long_press.press_time = generated_config::button::long_press_time_ms;


    // Registrar callback para el evento BUTTON_LONG_PRESS_UP
    ret = iot_button_register_cb(btn_handle, BUTTON_LONG_PRESS_UP, &long_press_args, button_long_press_cb, NULL);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error al registrar callback de long press: %s", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(TAG, "Callback de long press registrado en el botón");
    }

    // Registrar callback para el evento BUTTON_SINGLE_CLICK para alternar on/off
    ret = iot_button_register_cb(btn_handle, BUTTON_SINGLE_CLICK, NULL, button_short_press_cb, NULL);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error al registrar callback de short press: %s", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(TAG, "Callback de short press registrado en el botón");
    }

    return (app_driver_handle_t)btn_handle;
}
#endif // BUTTON_COUNT > 0

app_driver_handle_t app_driver_light_init()
{
#if LED_STRIP_LED_COUNT > 0
    ESP_LOGI(TAG, "Initializing LED strip light driver...");
    led_indicator_config_t indicator_config = {
        .mode = LED_STRIPS_MODE,
        .led_indicator_strips_config = NULL, // Se configurará por YAML
        // Los campos blink_lists y blink_list_num se omiten si CONFIG_LED_INDICATOR_ENABLE_CUSTOM_BLINK_LIST no está en sdkconfig
    };
    led_indicator_handle_t handle = led_indicator_create(&indicator_config);
    return (app_driver_handle_t)handle;
#else
    return NULL;
#endif
}
