#pragma once

#include "generated_config.h"

#include <esp_err.h>
#include <esp_matter.h>
#include <esp_matter_identify.h>

using app_driver_handle_t = void *;

struct DeviceModule {
    const char *name;
    app_driver_handle_t (*init_drivers)();
    bool (*supports_endpoint)(const generated_config::endpoint_raw &config);
    esp_matter::endpoint_t *(*create_endpoint)(const generated_config::endpoint_raw &config,
                                               esp_matter::node_t *node);
    void (*after_endpoint_created)(const generated_config::endpoint_raw &config,
                                   esp_matter::endpoint_t *endpoint);
    void (*apply_post_stack_start)();
    esp_err_t (*attribute_update)(app_driver_handle_t handle,
                                  uint16_t endpoint_id,
                                  uint32_t cluster_id,
                                  uint32_t attribute_id,
                                  esp_matter_attr_val_t *val);
    void (*perform_identification)(app_driver_handle_t handle,
                                   esp_matter::identification::callback_type_t type,
                                   uint8_t effect_id);
};
