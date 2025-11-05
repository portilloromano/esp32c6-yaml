#pragma once

#include <esp_matter.h>

namespace device_modules::utils {

bool lookup_device_type(const char *device_type, uint32_t &type_id, uint8_t &version);

bool add_descriptor_and_register(esp_matter::endpoint_t *endpoint,
                                 esp_matter::cluster::descriptor::config_t &descriptor_config,
                                 const char *device_type);

} // namespace device_modules::utils

