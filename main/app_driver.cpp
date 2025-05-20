#include <app_priv.h>

#include <stdio.h>
#include <led_strip.h>
#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_system.h>
#include <iot_button.h>
#include "driver/gpio.h"
#include "button_gpio.h"
#include "led_indicator.h"
#include <esp_matter.h>
#include <platform/ConfigurationManager.h>
#include <app/server/Server.h>

using namespace chip::app::Clusters;
using namespace esp_matter;

static const char *TAG = "app_driver";
extern uint16_t light_endpoint_id;

static esp_err_t app_driver_light_set_power(led_indicator_handle_t handle, esp_matter_attr_val_t *val)
{
#if LED_STRIP_LED_COUNT > 0
    if (val->val.b)
    {
        // Encender el LED de forma estática
        return led_indicator_set_on_off(handle, true);
    }
    else
    {
        // Apagar el LED
        return led_indicator_set_on_off(handle, false);
    }
#else
    ESP_LOGI(TAG, "LED set power: %d", val->val.b);
    return ESP_OK;
#endif
}

static esp_err_t app_driver_light_set_brightness(led_indicator_handle_t handle, esp_matter_attr_val_t *val)
{
    int value = REMAP_TO_RANGE(val->val.u8, MATTER_BRIGHTNESS, STANDARD_BRIGHTNESS);
#if LED_STRIP_LED_COUNT > 0
    return led_indicator_set_brightness(handle, value);
#else
    ESP_LOGI(TAG, "LED set brightness: %d", value);
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
    ESP_LOGI(TAG, "LED set hue: %d", value);
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
    ESP_LOGI(TAG, "LED set saturation: %d", value);
    return ESP_OK;
#endif
}

static esp_err_t app_driver_light_set_temperature(led_indicator_handle_t handle, esp_matter_attr_val_t *val)
{
    uint32_t value = REMAP_TO_RANGE_INVERSE(val->val.u16, STANDARD_TEMPERATURE_FACTOR);
#if LED_STRIP_LED_COUNT > 0
    return led_indicator_set_color_temperature(handle, value);
#else
    ESP_LOGI(TAG, "LED set temperature: %ld", value);
    return ESP_OK;
#endif
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
    esp_matter_attr_val_t val = esp_matter_invalid(NULL);
    attribute_t *attribute = nullptr; // Inicializar a nullptr

    /* Setting brightness */
    ESP_LOGI(TAG, "Getting attribute: LevelControl::CurrentLevel");
    attribute = attribute::get(endpoint_id, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);
    if (!attribute)
    {
        ESP_LOGE(TAG, "Failed to get attribute LevelControl::CurrentLevel (ID: 0x%04X)!", (unsigned int)LevelControl::Attributes::CurrentLevel::Id);
        // Considerar devolver ESP_FAIL aquí o manejar el error de alguna forma
    }
    else
    {
        ESP_LOGI(TAG, "Attribute LevelControl::CurrentLevel found.");
        err = attribute::get_val(attribute, &val);
        if (err == ESP_OK)
        {
            err |= app_driver_light_set_brightness(handle, &val);
        }
        else
        {
            ESP_LOGE(TAG, "Failed to get_val for CurrentLevel: %s", esp_err_to_name(err));
        }
    }

    /* Setting color */
    ESP_LOGI(TAG, "Getting attribute: ColorControl::ColorMode");
    attribute = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::ColorMode::Id);
    if (!attribute)
    {
        ESP_LOGE(TAG, "Failed to get attribute ColorControl::ColorMode (ID: 0x%04X)!", (unsigned int)ColorControl::Attributes::ColorMode::Id);
    }
    else
    {
        ESP_LOGI(TAG, "Attribute ColorControl::ColorMode found.");
        err = attribute::get_val(attribute, &val);
        if (err == ESP_OK)
        {
            if (val.val.u8 == (uint8_t)ColorControl::ColorMode::kColorTemperature)
            {
                ESP_LOGI(TAG, "ColorMode is kColorTemperature. Getting ColorTemperatureMireds.");
                attribute = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::ColorTemperatureMireds::Id);
                if (!attribute)
                {
                    ESP_LOGE(TAG, "Failed to get attribute ColorControl::ColorTemperatureMireds (ID: 0x%04X)!", (unsigned int)ColorControl::Attributes::ColorTemperatureMireds::Id);
                }
                else
                {
                    ESP_LOGI(TAG, "Attribute ColorControl::ColorTemperatureMireds found.");
                    err = attribute::get_val(attribute, &val);
                    if (err == ESP_OK)
                    {
                        err |= app_driver_light_set_temperature(handle, &val);
                    }
                    else
                    {
                        ESP_LOGE(TAG, "Failed to get_val for ColorTemperatureMireds: %s", esp_err_to_name(err));
                    }
                }
            }
            else if (val.val.u8 == (uint8_t)ColorControl::ColorMode::kCurrentHueAndCurrentSaturation)
            {
                ESP_LOGI(TAG, "ColorMode is kCurrentHueAndCurrentSaturation.");
                // Añade verificaciones similares para CurrentHue y CurrentSaturation aquí
                attribute = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentHue::Id);
                if (!attribute)
                {
                    ESP_LOGE(TAG, "Attribute CurrentHue is NULL!");
                }
                else
                { /*...*/
                }

                attribute = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentSaturation::Id);
                if (!attribute)
                {
                    ESP_LOGE(TAG, "Attribute CurrentSaturation is NULL!");
                }
                else
                { /*...*/
                }
            }
            else
            {
                ESP_LOGE(TAG, "Color mode 0x%02X not fully handled for defaults in this example", val.val.u8);
            }
        }
        else
        {
            ESP_LOGE(TAG, "Failed to get_val for ColorMode: %s", esp_err_to_name(err));
        }
    }

    /* Setting power */
    ESP_LOGI(TAG, "Getting attribute: OnOff::OnOff");
    attribute = attribute::get(endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id);
    if (!attribute)
    {
        ESP_LOGE(TAG, "Failed to get attribute OnOff::OnOff (ID: 0x%04X)!", (unsigned int)OnOff::Attributes::OnOff::Id);
    }
    else
    {
        ESP_LOGI(TAG, "Attribute OnOff::OnOff found.");
        err = attribute::get_val(attribute, &val);
        if (err == ESP_OK)
        {
            err |= app_driver_light_set_power(handle, &val);
        }
        else
        {
            ESP_LOGE(TAG, "Failed to get_val for OnOff: %s", esp_err_to_name(err));
        }
    }

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error occurred while setting driver defaults.");
    }
    else
    {
        ESP_LOGI(TAG, "Driver defaults set successfully for endpoint %u.", endpoint_id);
    }
    return err;
}

// Callback que se ejecuta cuando se suelta el botón tras una presión larga (>=5000 ms)
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
    // Obtener el endpoint usando el ID global light_endpoint_id
    endpoint_t *ep = endpoint::get(light_endpoint_id);
    if (ep == NULL)
    {
        ESP_LOGE(TAG, "No se encontró el endpoint con ID: %d", light_endpoint_id);
        return;
    }

    // Obtener el cluster OnOff desde el endpoint
    cluster_t *onoff_cluster = cluster::get(ep, OnOff::Id);
    if (onoff_cluster == NULL)
    {
        ESP_LOGE(TAG, "No se encontró el cluster OnOff");
        return;
    }

    // Obtener el atributo OnOff
    attribute_t *onoff_attr = attribute::get(onoff_cluster, OnOff::Attributes::OnOff::Id);
    if (onoff_attr == NULL)
    {
        ESP_LOGE(TAG, "No se encontró el atributo OnOff");
        return;
    }

    // Leer el valor actual del atributo
    esp_matter_attr_val_t current_val = esp_matter_invalid(NULL);
    attribute::get_val(onoff_attr, &current_val);
    bool current_state = current_val.val.b;

    // Toggle del estado: si está encendido, se apaga; si está apagado, se enciende.
    bool new_state = !current_state;
    esp_matter_attr_val_t new_val = esp_matter_bool(new_state);

    if (new_state)
    {
        ESP_LOGI(TAG, "Single click: encendiendo LED");
    }
    else
    {
        ESP_LOGI(TAG, "Single click: apagando LED");
    }

    // Obtener el handle del driver desde los datos privados del endpoint
    void *driver_handle = endpoint::get_priv_data(light_endpoint_id);
    if (driver_handle == NULL)
    {
        ESP_LOGE(TAG, "No se encontró el driver handle en el endpoint");
        return;
    }

    // Actualizar el estado del LED usando la función del driver
    esp_err_t err = app_driver_light_set_power(driver_handle, &new_val);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error al actualizar el estado del LED: %s", esp_err_to_name(err));
        return;
    }

    // Actualizar el valor del atributo para reflejar el cambio de estado
    // attribute::set_val(onoff_attr, &new_val);

    attribute::update(light_endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id, &new_val);
}

app_driver_handle_t app_driver_light_init()
{
#if LED_STRIP_LED_COUNT > 0
    // Configuración para el LED indicator en modo LED_STRIPS_MODE.
    // Se declara como static para que la memoria persista.
    static led_indicator_strips_config_t strips_config = {
        .led_strip_cfg = {
            .strip_gpio_num = LED_GPIO,
            .max_leds = LED_STRIP_LED_COUNT,
            .led_model = LED_MODEL,
            // Inicializa otros campos si es necesario.
        },
        .led_strip_driver = LED_STRIP_RMT, // O el driver que utilices (por ejemplo, LED_STRIP_SPI).
        .led_strip_rmt_cfg = {
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = 10 * 1000 * 1000,
            .mem_block_symbols = 64,
            .flags = {.with_dma = false},
        },
    };

    led_indicator_config_t indicator_config = {
        .mode = LED_STRIPS_MODE,                       // Modo que se adapte a tu hardware.
        .led_indicator_strips_config = &strips_config, // Puntero a la configuración.
        .blink_lists = NULL,                           // Sin patrones de blink predefinidos.
        .blink_list_num = 0,
    };

    // Crear la instancia del LED indicator.
    led_indicator_handle_t indicator = led_indicator_create(&indicator_config);
    if (indicator == NULL)
    {
        ESP_LOGE(TAG, "Error creando el LED indicator");
        return NULL;
    }

    // Establecer el color por defecto (usa los valores que necesites).
    ESP_ERROR_CHECK(led_indicator_set_hsv(indicator, SET_HSV(DEFAULT_HUE, DEFAULT_SATURATION, DEFAULT_BRIGHTNESS)));

    return (app_driver_handle_t)indicator;
#else
    return NULL;
#endif
}

app_driver_handle_t app_driver_button_init()
{
    button_config_t btn_config = {
        .long_press_time = APP_BUTTON_LONG_PRESS_TIME_MS,
        .short_press_time = APP_BUTTON_SHORT_PRESS_TIME_MS,
    };

    // Configurar el driver GPIO usando button_gpio_config_t
    button_gpio_config_t gpio_config = {
        .gpio_num = BUTTON_GPIO,
        .active_level = BUTTON_ACTIVE_LEVEL,
        .enable_power_save = false, // Puedes habilitar power save si lo necesitas
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
    long_press_args.long_press.press_time = 5000; // 5000 ms

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

    // Registrar callback para el evento BUTTON_SHORT_PRESS_UP para alternar on/off
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
