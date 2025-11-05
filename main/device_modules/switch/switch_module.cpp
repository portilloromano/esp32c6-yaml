#include "switch_module.h"

#include "generated_config.h"

#include <cstring>
#include <esp_log.h>
#include <esp_matter_cluster.h>
#include <esp_matter_endpoint.h>

namespace device_modules::switch_module {

using namespace esp_matter;

namespace {

constexpr const char *TAG = "switch_module";

struct identify_cluster_config {
    bool enabled;
    uint16_t identify_time;
    uint8_t identify_type;
};

struct endpoint_config_resolved {
    uint16_t id;
    const char *device_type;
    identify_cluster_config identify;
};

app_driver_handle_t init_drivers()
{
    // Currently no dedicated hardware driver required for the switch module.
    return nullptr;
}

bool streq(const char *lhs, const char *rhs)
{
    if (!lhs || !rhs) {
        return false;
    }
    return std::strcmp(lhs, rhs) == 0;
}

bool device_has_default_cluster(const char *device_type, const char *cluster)
{
    return streq(device_type, "on_off_switch") &&
           (streq(cluster, "identify") || streq(cluster, "on_off"));
}

bool compute_enabled_flag(bool present, const generated_config::optional_bool &explicit_value, bool default_enabled)
{
    if (explicit_value.has_value) {
        return explicit_value.value;
    }
    if (present) {
        return true;
    }
    return default_enabled;
}

int optional_int_value(const generated_config::optional_int &value, int fallback)
{
    return value.has_value ? static_cast<int>(value.value) : fallback;
}

endpoint_config_resolved resolve_switch_config(const generated_config::endpoint_raw &raw)
{
    endpoint_config_resolved resolved{};
    resolved.id = raw.id;
    resolved.device_type = raw.device_type;

    resolved.identify.enabled = compute_enabled_flag(raw.identify.present,
                                                     raw.identify.enabled,
                                                     device_has_default_cluster(raw.device_type, "identify"));
    resolved.identify.identify_time = static_cast<uint16_t>(optional_int_value(raw.identify.identify_time, 0));
    resolved.identify.identify_type = static_cast<uint8_t>(optional_int_value(raw.identify.identify_type, 0));

    return resolved;
}

bool supports_endpoint(const generated_config::endpoint_raw &config)
{
    return streq(config.device_type, "on_off_switch");
}

void apply_common_config(endpoint::on_off_switch::config_t &cfg,
                         const endpoint_config_resolved &ep_config)
{
    if (ep_config.identify.enabled) {
        cfg.identify.identify_time = ep_config.identify.identify_time;
        cfg.identify.identify_type = ep_config.identify.identify_type;
    }
}

endpoint_t *create_endpoint(const generated_config::endpoint_raw &config, node_t *node)
{
    if (!supports_endpoint(config)) {
        return nullptr;
    }

    endpoint_config_resolved resolved = resolve_switch_config(config);

    endpoint::on_off_switch::config_t cfg;
    apply_common_config(cfg, resolved);

    endpoint_t *endpoint = endpoint::on_off_switch::create(node, &cfg, ENDPOINT_FLAG_NONE, nullptr);
    if (!endpoint) {
        ESP_LOGE(TAG, "Failed to create endpoint for device type %s", resolved.device_type);
        return nullptr;
    }
    return endpoint;
}

void after_endpoint_created(const generated_config::endpoint_raw &, endpoint_t *)
{
    // No additional bookkeeping required for the switch module.
}

void apply_post_stack_start()
{
    // Nothing to do for switch module after the Matter stack starts.
}

} // namespace

const DeviceModule kModule = {
    .name = "switch",
    .init_drivers = init_drivers,
    .supports_endpoint = supports_endpoint,
    .create_endpoint = create_endpoint,
    .after_endpoint_created = after_endpoint_created,
    .apply_post_stack_start = apply_post_stack_start,
    .attribute_update = nullptr,
    .perform_identification = nullptr,
};

} // namespace device_modules::switch_module
