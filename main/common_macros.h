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
 * @brief Remapea linealmente un valor de un rango [0..src_max] a [0..dst_max].
 *
 * Garantiza clamp del valor de entrada y evita divisiones por cero.
 */
inline int remap_to_range(int value, int src_max, int dst_max)
{
    if (src_max == 0 || dst_max == 0) {
        return 0;
    }

    const int clamped_value = CLAMPI(value, 0, src_max);
    const int64_t numerator = static_cast<int64_t>(clamped_value) * static_cast<int64_t>(dst_max);
    return static_cast<int>(numerator / static_cast<int64_t>(src_max));
}

/**
 * @brief Conversión inversa típica para temperatura de color.
 *
 * Convierte mireds a Kelvin aproximados usando un factor definido.
 * FACTOR = 1,000,000 (1e6) para la fórmula Kelvin = 1,000,000 / Mireds.
 */
inline uint32_t remap_to_range_inverse(uint32_t mireds, uint32_t factor)
{
    if (mireds == 0U) {
        return 0U;
    }

    const uint64_t numerator = static_cast<uint64_t>(factor) + static_cast<uint64_t>(mireds >> 1);
    return static_cast<uint32_t>(numerator / static_cast<uint64_t>(mireds));
}
