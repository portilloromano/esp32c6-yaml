#include "endpoint_utils.h"

#include <cstring>
#include <esp_log.h>
#include <esp_matter_cluster.h>
#include <esp_matter_endpoint.h>

namespace device_modules::utils {

using namespace esp_matter;
using namespace esp_matter::cluster;

namespace {
struct DeviceTypeInfo {
    const char *name;
    uint32_t id;
    uint8_t version;
};

constexpr DeviceTypeInfo kDeviceTypes[] = {
    {"on_off_light", ESP_MATTER_ON_OFF_LIGHT_DEVICE_TYPE_ID, ESP_MATTER_ON_OFF_LIGHT_DEVICE_TYPE_VERSION},
    {"dimmable_light", ESP_MATTER_DIMMABLE_LIGHT_DEVICE_TYPE_ID, ESP_MATTER_DIMMABLE_LIGHT_DEVICE_TYPE_VERSION},
    {"extended_color_light", ESP_MATTER_EXTENDED_COLOR_LIGHT_DEVICE_TYPE_ID, ESP_MATTER_EXTENDED_COLOR_LIGHT_DEVICE_TYPE_VERSION},
    {"on_off_switch", ESP_MATTER_ON_OFF_SWITCH_DEVICE_TYPE_ID, ESP_MATTER_ON_OFF_SWITCH_DEVICE_TYPE_VERSION},
};

constexpr const char *TAG = "endpoint_utils";
} // namespace

bool lookup_device_type(const char *device_type, uint32_t &type_id, uint8_t &version)
{
    for (const auto &entry : kDeviceTypes) {
        if (strcmp(entry.name, device_type) == 0) {
            type_id = entry.id;
            version = entry.version;
            return true;
        }
    }
    return false;
}

bool add_descriptor_and_register(esp_matter::endpoint_t *endpoint,
                                 esp_matter::cluster::descriptor::config_t &descriptor_config,
                                 const char *device_type)
{
    cluster_t *descriptor_cluster = descriptor::create(endpoint, &descriptor_config, CLUSTER_FLAG_SERVER);
    if (!descriptor_cluster) {
        ESP_LOGE(TAG, "Failed to create descriptor cluster");
        return false;
    }

    uint32_t type_id = 0;
    uint8_t version = 0;
    if (!lookup_device_type(device_type, type_id, version)) {
        ESP_LOGE(TAG, "Unsupported device type in config.yaml: %s", device_type);
        return false;
    }

    if (endpoint::add_device_type(endpoint, type_id, version) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register device type %s", device_type);
        return false;
    }

    return true;
}

} // namespace device_modules::utils
