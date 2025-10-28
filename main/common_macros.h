// Copyright 2024
// Archivo de utilidades y macros comunes para el proyecto ESP32-C6 con ESP-Matter.

#pragma once

#include <stdlib.h>
#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/**
 * @brief Aborta la aplicación si la condición no se cumple.
 *
 * Ejemplo:
 * ABORT_APP_ON_FAILURE(ptr != NULL, ESP_LOGE(TAG, "Pointer is NULL"));
 */
#define ABORT_APP_ON_FAILURE(x, ...)               \
    do                                             \
    {                                              \
        if (!(unlikely(x)))                        \
        {                                          \
            __VA_ARGS__;                           \
            vTaskDelay(5000 / portTICK_PERIOD_MS); \
            abort();                               \
        }                                          \
    } while (0)

/**
 * @brief Limita un valor entero a un rango [lo, hi].
 */
#ifndef CLAMPI
#define CLAMPI(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))
#endif

/**
 * @brief Remapea linealmente un valor de un rango [0..SRC_MAX] a [0..DST_MAX].
 *
 * Garantiza clamp del valor de entrada y evita divisiones por cero.
 *
 * Ejemplo:
 * int brillo = REMAP_TO_RANGE(matter_val, MATTER_BRIGHTNESS, STANDARD_BRIGHTNESS);
 */
#ifndef REMAP_TO_RANGE
#define REMAP_TO_RANGE(x, SRC_MAX, DST_MAX)                     \
    ({                                                          \
        const int _smax = (int)(SRC_MAX);                       \
        const int _dmax = (int)(DST_MAX);                       \
        const int _sx = CLAMPI((int)(x), 0, _smax);             \
        int _res;                                               \
        if (_smax == 0)                                         \
        {                                                       \
            _res = 0;                                           \
        }                                                       \
        else                                                    \
        {                                                       \
            const int64_t _num = (int64_t)_sx * (int64_t)_dmax; \
            const int64_t _den = (int64_t)_smax;                \
            _res = (int)(_num / _den);                          \
        }                                                       \
        _res;                                                   \
    })
#endif

/**
 * @brief Conversión inversa típica para temperatura de color.
 *
 * Convierte mireds a Kelvin aproximados usando un factor definido.
 * FACTOR = 1,000,000 (1e6) para la fórmula Kelvin = 1,000,000 / Mireds.
 *
 * Ejemplo:
 * uint32_t kelvin = REMAP_TO_RANGE_INVERSE(mireds, STANDARD_TEMPERATURE_FACTOR);
 */
#ifndef REMAP_TO_RANGE_INVERSE
#define REMAP_TO_RANGE_INVERSE(mireds, FACTOR)                               \
    ({                                                                       \
        const uint32_t _m = (uint32_t)(mireds);                              \
        const uint32_t _f = (uint32_t)(FACTOR);                              \
        uint32_t _res = (_m == 0U) ? 0U : (uint32_t)((_f + (_m >> 1)) / _m); \
        _res;                                                                \
    })
#endif
