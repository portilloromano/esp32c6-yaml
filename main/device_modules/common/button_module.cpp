#include "common/button_module.h"

#include "generated_config.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <cstdio>
#include <inttypes.h>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <iot_button.h>
#include <driver/gpio.h>
#include "button_gpio.h"

#include <esp_matter.h>
#include <esp_matter_cluster.h>
#include <esp_matter_endpoint.h>
#include <esp_matter_attribute.h>
#include <esp_matter_core.h>
#include <esp_matter_client.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <platform/CHIPDeviceLayer.h>
#include <lib/core/DataModelTypes.h>
#include <lib/core/Optional.h>
#include <app-common/zap-generated/cluster-objects.h>

namespace device_modules::button {

using namespace chip::app::Clusters;
using namespace esp_matter;
using namespace esp_matter::cluster;
using namespace esp_matter::attribute;

namespace {

constexpr const char *TAG = "button_driver";

using ButtonConfig = generated_config::button::config_t;
constexpr size_t kButtonCount = generated_config::button::count;

struct IdentifyCommandPayload {
    uint16_t duration_s;
};

enum class ButtonMode { Remote, Local, Dual };
enum class ActionCluster { OnOff, Identify, Unsupported };
enum class ActionCommand { Toggle, On, Off, Identify, Unsupported };

struct ButtonRuntime {
    const ButtonConfig *cfg = nullptr;
    button_handle_t handle = nullptr;
    ButtonMode mode = ButtonMode::Remote;
    ActionCluster cluster = ActionCluster::OnOff;
    ActionCommand command = ActionCommand::Toggle;
    chip::EndpointId binding_endpoint = chip::kInvalidEndpointId;
    chip::EndpointId target_endpoint = chip::kInvalidEndpointId;
    uint8_t short_press_count = 0;
    TickType_t last_short_press_tick = 0;
    IdentifyCommandPayload identify_payload{0};
};

static std::array<ButtonRuntime, kButtonCount> s_button_states{};
static bool s_client_callbacks_registered = false;

const char *button_name(const ButtonRuntime &btn)
{
    return (btn.cfg && btn.cfg->id) ? btn.cfg->id : "button";
}

ButtonMode parse_mode(const char *mode)
{
    if (!mode) {
        return ButtonMode::Remote;
    }
    if (std::strcmp(mode, "local") == 0) {
        return ButtonMode::Local;
    }
    if (std::strcmp(mode, "dual") == 0) {
        return ButtonMode::Dual;
    }
    if (std::strcmp(mode, "remote") != 0) {
        ESP_LOGW(TAG, "Unknown button mode '%s', defaulting to remote.", mode);
    }
    return ButtonMode::Remote;
}

ActionCluster parse_cluster(const char *cluster)
{
    if (!cluster || std::strcmp(cluster, "on_off") == 0) {
        return ActionCluster::OnOff;
    }
    if (std::strcmp(cluster, "identify") == 0) {
        return ActionCluster::Identify;
    }
    ESP_LOGW(TAG, "Unsupported action cluster '%s'.", cluster ? cluster : "<null>");
    return ActionCluster::Unsupported;
}

ActionCommand parse_command(ActionCluster cluster, const char *command)
{
    if (cluster == ActionCluster::OnOff) {
        if (!command || std::strcmp(command, "toggle") == 0) {
            return ActionCommand::Toggle;
        }
        if (std::strcmp(command, "on") == 0) {
            return ActionCommand::On;
        }
        if (std::strcmp(command, "off") == 0) {
            return ActionCommand::Off;
        }
        ESP_LOGW(TAG, "Unsupported on_off command '%s', defaulting to toggle.", command ? command : "<null>");
        return ActionCommand::Toggle;
    }
    if (cluster == ActionCluster::Identify) {
        return ActionCommand::Identify;
    }
    return ActionCommand::Unsupported;
}

bool mode_has_remote(ButtonMode mode)
{
    return mode == ButtonMode::Remote || mode == ButtonMode::Dual;
}

bool mode_has_local(ButtonMode mode)
{
    return mode == ButtonMode::Local || mode == ButtonMode::Dual;
}

chip::EndpointId resolve_default_binding_endpoint()
{
    for (size_t idx = 0; idx < generated_config::num_endpoints; ++idx) {
        const auto &endpoint = generated_config::endpoints[idx];
        if (endpoint.device_type && std::strcmp(endpoint.device_type, "on_off_switch") == 0) {
            return static_cast<chip::EndpointId>(endpoint.id);
        }
    }
    return chip::kInvalidEndpointId;
}

chip::EndpointId resolve_default_local_endpoint()
{
    for (size_t idx = 0; idx < generated_config::num_endpoints; ++idx) {
        const auto &endpoint = generated_config::endpoints[idx];
        if (!endpoint.device_type) {
            continue;
        }
        if (endpoint.on_off.present && std::strcmp(endpoint.device_type, "on_off_switch") != 0) {
            return static_cast<chip::EndpointId>(endpoint.id);
        }
    }
    return chip::kInvalidEndpointId;
}

static void send_command_success_callback(void *, const chip::app::ConcreteCommandPath &,
                                          const chip::app::StatusIB &, chip::TLV::TLVReader *)
{
    ESP_LOGD(TAG, "Command sent successfully.");
}

static void send_command_failure_callback(void *, CHIP_ERROR error)
{
    ESP_LOGE(TAG, "Command send failed: %" CHIP_ERROR_FORMAT, error.Format());
}

static void button_client_invoke_cb(client::peer_device_t *peer_device,
                                    client::request_handle_t *req_handle,
                                    void *);
static void button_client_group_invoke_cb(uint8_t fabric_index,
                                          client::request_handle_t *req_handle,
                                          void *);

esp_err_t ensure_client_callbacks()
{
    if (s_client_callbacks_registered) {
        return ESP_OK;
    }
#ifdef CONFIG_ESP_MATTER_ENABLE_MATTER_SERVER
    esp_matter::client::binding_init();
#endif

    esp_err_t err = esp_matter::client::set_request_callback(button_client_invoke_cb,
                                                             button_client_group_invoke_cb,
                                                             nullptr);
    if (err == ESP_OK) {
        s_client_callbacks_registered = true;
    } else {
        ESP_LOGE(TAG, "Failed to register client callbacks: %s", esp_err_to_name(err));
    }
    return err;
}

chip::EndpointId binding_endpoint_for(const ButtonRuntime &btn)
{
    if (btn.binding_endpoint != chip::kInvalidEndpointId) {
        return btn.binding_endpoint;
    }
    return resolve_default_binding_endpoint();
}

chip::EndpointId target_endpoint_for(const ButtonRuntime &btn)
{
    if (btn.target_endpoint != chip::kInvalidEndpointId) {
        return btn.target_endpoint;
    }
    return resolve_default_local_endpoint();
}

esp_err_t dispatch_bound_command(ButtonRuntime &btn, client::request_handle_t &req_handle)
{
    chip::EndpointId local_endpoint = binding_endpoint_for(btn);
    if (local_endpoint == chip::kInvalidEndpointId) {
        ESP_LOGW(TAG, "%s: no binding endpoint available for remote command.", button_name(btn));
        return ESP_ERR_INVALID_STATE;
    }

    endpoint_t *endpoint = endpoint::get(local_endpoint);
    if (!endpoint) {
        ESP_LOGW(TAG, "%s: endpoint %u not created yet; retrying later.",
                 button_name(btn), static_cast<unsigned int>(local_endpoint));
        return ESP_ERR_INVALID_STATE;
    }

    auto lock_status = esp_matter::lock::chip_stack_lock(portMAX_DELAY);
    if (lock_status != esp_matter::lock::status::SUCCESS) {
        ESP_LOGE(TAG, "%s: failed to acquire CHIP stack lock (status=%d).",
                 button_name(btn), static_cast<int>(lock_status));
        return ESP_FAIL;
    }

    esp_err_t err = esp_matter::client::cluster_update(static_cast<uint16_t>(local_endpoint), &req_handle);
    esp_matter::lock::chip_stack_unlock();

    if (err == ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "%s: no bindings configured for endpoint %u.",
                 button_name(btn), static_cast<unsigned int>(local_endpoint));
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s: cluster_update failed: %s", button_name(btn), esp_err_to_name(err));
    }
    return err;
}

esp_err_t send_remote_onoff(ButtonRuntime &btn)
{
    if (btn.cluster != ActionCluster::OnOff ||
        !mode_has_remote(btn.mode)) {
        return ESP_OK;
    }
    if (ensure_client_callbacks() != ESP_OK) {
        return ESP_FAIL;
    }

    chip::CommandId command_id = OnOff::Commands::Toggle::Id;
    switch (btn.command) {
    case ActionCommand::On:
        command_id = OnOff::Commands::On::Id;
        break;
    case ActionCommand::Off:
        command_id = OnOff::Commands::Off::Id;
        break;
    case ActionCommand::Toggle:
        command_id = OnOff::Commands::Toggle::Id;
        break;
    default:
        ESP_LOGW(TAG, "%s: unsupported on_off command for remote execution.", button_name(btn));
        return ESP_ERR_INVALID_ARG;
    }

    client::request_handle_t req_handle;
    req_handle.type = client::INVOKE_CMD;
    req_handle.command_path.mClusterId = OnOff::Id;
    req_handle.command_path.mCommandId = command_id;
    req_handle.request_data = nullptr;

    return dispatch_bound_command(btn, req_handle);
}

esp_err_t send_remote_identify(ButtonRuntime &btn, uint16_t duration_s)
{
    if (!mode_has_remote(btn.mode)) {
        return ESP_OK;
    }
    if (ensure_client_callbacks() != ESP_OK) {
        return ESP_FAIL;
    }

    btn.identify_payload.duration_s = duration_s;

    client::request_handle_t req_handle;
    req_handle.type = client::INVOKE_CMD;
    req_handle.command_path.mClusterId = Identify::Id;
    req_handle.command_path.mCommandId = Identify::Commands::Identify::Id;
    req_handle.request_data = &btn.identify_payload;

    return dispatch_bound_command(btn, req_handle);
}

esp_err_t perform_local_onoff(ButtonRuntime &btn)
{
    if (btn.cluster != ActionCluster::OnOff ||
        !mode_has_local(btn.mode)) {
        return ESP_OK;
    }

    chip::EndpointId endpoint_id = target_endpoint_for(btn);
    if (endpoint_id == chip::kInvalidEndpointId) {
        ESP_LOGW(TAG, "%s: no local endpoint available for on/off control.", button_name(btn));
        return ESP_ERR_INVALID_STATE;
    }

    attribute_t *attr = attribute::get(endpoint_id,
                                       OnOff::Id,
                                       OnOff::Attributes::OnOff::Id);
    if (!attr) {
        ESP_LOGW(TAG, "%s: OnOff attribute not found on endpoint %u.",
                 button_name(btn), static_cast<unsigned int>(endpoint_id));
        return ESP_ERR_INVALID_STATE;
    }

    bool new_state = true;
    if (btn.command == ActionCommand::Toggle) {
        esp_matter_attr_val_t current_val = esp_matter_invalid(nullptr);
        if (attribute::get_val(attr, &current_val) == ESP_OK) {
            new_state = !current_val.val.b;
        } else {
            ESP_LOGW(TAG, "%s: failed to read current OnOff state; defaulting to ON.", button_name(btn));
            new_state = true;
        }
    } else if (btn.command == ActionCommand::On) {
        new_state = true;
    } else if (btn.command == ActionCommand::Off) {
        new_state = false;
    } else {
        ESP_LOGW(TAG, "%s: unsupported local on_off command.", button_name(btn));
        return ESP_ERR_INVALID_ARG;
    }

    esp_matter_attr_val_t new_val = esp_matter_bool(new_state);
    esp_err_t err = attribute::update(endpoint_id,
                                      OnOff::Id,
                                      OnOff::Attributes::OnOff::Id,
                                      &new_val);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s: failed to update OnOff attribute: %s",
                 button_name(btn), esp_err_to_name(err));
    }
    return err;
}

esp_err_t perform_local_identify(ButtonRuntime &btn, uint16_t duration_s)
{
    if (!mode_has_local(btn.mode)) {
        return ESP_OK;
    }

    chip::EndpointId endpoint_id = target_endpoint_for(btn);
    if (endpoint_id == chip::kInvalidEndpointId) {
        ESP_LOGW(TAG, "%s: no local endpoint available for identify.", button_name(btn));
        return ESP_ERR_INVALID_STATE;
    }

    esp_matter_attr_val_t identify_val = esp_matter_uint16(duration_s);
    esp_err_t err = attribute::update(endpoint_id,
                                      Identify::Id,
                                      Identify::Attributes::IdentifyTime::Id,
                                      &identify_val);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s: failed to update IdentifyTime: %s",
                 button_name(btn), esp_err_to_name(err));
    }
    return err;
}

void handle_button_action(ButtonRuntime &btn)
{
    const auto &cfg = *btn.cfg;
    const uint16_t action_identify_time = static_cast<uint16_t>(
        cfg.action_identify_time_s > 0 ? cfg.action_identify_time_s : cfg.identify_time_s);

    switch (btn.cluster) {
    case ActionCluster::OnOff:
        send_remote_onoff(btn);
        perform_local_onoff(btn);
        break;
    case ActionCluster::Identify:
        send_remote_identify(btn, action_identify_time);
        perform_local_identify(btn, action_identify_time);
        break;
    default:
        ESP_LOGW(TAG, "%s: cluster action not supported.", button_name(btn));
        break;
    }
}

static void button_client_invoke_cb(client::peer_device_t *peer_device,
                                    client::request_handle_t *req_handle,
                                    void *)
{
    if (!req_handle || req_handle->type != client::INVOKE_CMD) {
        return;
    }

    char command_data_str[32];
    if (req_handle->command_path.mClusterId == OnOff::Id) {
        std::strcpy(command_data_str, "{}");
    } else if (req_handle->command_path.mClusterId == Identify::Id &&
               req_handle->command_path.mCommandId == Identify::Commands::Identify::Id) {
        uint16_t duration = 0;
        if (req_handle->request_data) {
            const auto *payload = static_cast<const IdentifyCommandPayload *>(req_handle->request_data);
            duration = payload->duration_s;
        }
        std::snprintf(command_data_str, sizeof(command_data_str), "{\"0:U16\": %u}", duration);
    } else {
        ESP_LOGW(TAG, "Unsupported cluster 0x%08" PRIx32 " for invoke callback.",
                 static_cast<uint32_t>(req_handle->command_path.mClusterId));
        return;
    }

    esp_matter::client::interaction::invoke::send_request(nullptr,
                                                          peer_device,
                                                          req_handle->command_path,
                                                          command_data_str,
                                                          send_command_success_callback,
                                                          send_command_failure_callback,
                                                          chip::NullOptional);
}

static void button_client_group_invoke_cb(uint8_t fabric_index,
                                          client::request_handle_t *req_handle,
                                          void *)
{
    if (!req_handle || req_handle->type != client::INVOKE_CMD) {
        return;
    }

    char command_data_str[32];
    if (req_handle->command_path.mClusterId == OnOff::Id) {
        std::strcpy(command_data_str, "{}");
    } else if (req_handle->command_path.mClusterId == Identify::Id &&
               req_handle->command_path.mCommandId == Identify::Commands::Identify::Id) {
        uint16_t duration = 0;
        if (req_handle->request_data) {
            const auto *payload = static_cast<const IdentifyCommandPayload *>(req_handle->request_data);
            duration = payload->duration_s;
        }
        std::snprintf(command_data_str, sizeof(command_data_str), "{\"0:U16\": %u}", duration);
    } else {
        ESP_LOGW(TAG, "Unsupported cluster 0x%08" PRIx32 " for group callback.",
                 static_cast<uint32_t>(req_handle->command_path.mClusterId));
        return;
    }

    esp_matter::client::interaction::invoke::send_group_request(fabric_index,
                                                                req_handle->command_path,
                                                                command_data_str);
}

static void button_long_press_cb(void *, void *usr_data)
{
    auto *state = static_cast<ButtonRuntime *>(usr_data);
    const char *name = state ? button_name(*state) : "button";

    ESP_LOGI(TAG, "%s: long press detected, erasing NVM...", name);
    esp_err_t ret = nvs_flash_erase();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "%s: NVM erased, reinitializing...", name);
        ret = nvs_flash_init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "%s: failed to reinitialize NVM: %s", name, esp_err_to_name(ret));
        }
    } else {
        ESP_LOGE(TAG, "%s: failed to erase NVM: %s", name, esp_err_to_name(ret));
    }
    esp_restart();
}

static void button_short_press_cb(void *, void *usr_data)
{
    auto *state = static_cast<ButtonRuntime *>(usr_data);
    if (!state || !state->cfg) {
        return;
    }

    const ButtonConfig &cfg = *state->cfg;
    TickType_t current_tick = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(cfg.short_press_timeout_ms > 0 ? cfg.short_press_timeout_ms : 0);

    if (state->short_press_count == 0 ||
        timeout_ticks == 0 ||
        (current_tick - state->last_short_press_tick) > timeout_ticks) {
        state->short_press_count = 1;
    } else {
        ++state->short_press_count;
    }
    state->last_short_press_tick = current_tick;

    ESP_LOGI(TAG, "%s: short press count = %u",
             button_name(*state), static_cast<unsigned int>(state->short_press_count));

    handle_button_action(*state);

    if (cfg.identify_trigger_count > 0 &&
        state->short_press_count >= static_cast<uint8_t>(cfg.identify_trigger_count)) {
        ESP_LOGI(TAG,
                 "%s: identify trigger reached (%d presses).",
                 button_name(*state), cfg.identify_trigger_count);
        if (mode_has_remote(state->mode)) {
            send_remote_identify(*state, static_cast<uint16_t>(cfg.identify_time_s));
        }
        if (mode_has_local(state->mode)) {
            perform_local_identify(*state, static_cast<uint16_t>(cfg.identify_time_s));
        }
        state->short_press_count = 0;
    }
}

} // namespace

app_driver_handle_t init()
{
    if constexpr (kButtonCount == 0) {
        ESP_LOGI(TAG, "No buttons configured.");
        return nullptr;
    }

    app_driver_handle_t primary_handle = nullptr;
    bool needs_client_callbacks = false;

    for (size_t idx = 0; idx < kButtonCount; ++idx) {
        ButtonRuntime &state = s_button_states[idx];
        const ButtonConfig &cfg = generated_config::button::configs[idx];

        state.cfg = &cfg;
        state.handle = nullptr;
        state.short_press_count = 0;
        state.last_short_press_tick = 0;
        state.mode = parse_mode(cfg.mode);
        state.cluster = parse_cluster(cfg.action_cluster);
        state.command = parse_command(state.cluster, cfg.action_command);
        state.identify_payload.duration_s = static_cast<uint16_t>(
            cfg.action_identify_time_s > 0 ? cfg.action_identify_time_s : cfg.identify_time_s);

        state.binding_endpoint = cfg.binding_endpoint > 0
                                     ? static_cast<chip::EndpointId>(cfg.binding_endpoint)
                                     : chip::kInvalidEndpointId;
        state.target_endpoint = cfg.target_endpoint > 0
                                    ? static_cast<chip::EndpointId>(cfg.target_endpoint)
                                    : chip::kInvalidEndpointId;

        if (mode_has_remote(state.mode)) {
            needs_client_callbacks = true;
            if (state.binding_endpoint == chip::kInvalidEndpointId) {
                state.binding_endpoint = resolve_default_binding_endpoint();
            }
        }
        if (mode_has_local(state.mode) && state.target_endpoint == chip::kInvalidEndpointId) {
            state.target_endpoint = resolve_default_local_endpoint();
        }

        button_gpio_config_t gpio_cfg = {
            .gpio_num = static_cast<gpio_num_t>(cfg.gpio),
            .active_level = static_cast<uint8_t>(cfg.active_level),
            .enable_power_save = false,
            .disable_pull = false,
        };

        button_config_t btn_cfg = {
            .long_press_time = static_cast<uint16_t>(std::clamp(cfg.long_press_time_ms, 0, 0xFFFF)),
            .short_press_time = 50,
        };

        button_handle_t handle = nullptr;
        esp_err_t err = iot_button_new_gpio_device(&btn_cfg, &gpio_cfg, &handle);
        if (err != ESP_OK || handle == nullptr) {
            ESP_LOGE(TAG, "%s: failed to create button (gpio %d): %s",
                     button_name(state), cfg.gpio, esp_err_to_name(err));
            continue;
        }

        state.handle = handle;
        if (!primary_handle) {
            primary_handle = handle;
        }

        err = iot_button_register_cb(handle, BUTTON_LONG_PRESS_UP, nullptr, button_long_press_cb, &state);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "%s: failed to register long press callback: %s",
                     button_name(state), esp_err_to_name(err));
        }

        err = iot_button_register_cb(handle, BUTTON_SINGLE_CLICK, nullptr, button_short_press_cb, &state);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "%s: failed to register short press callback: %s",
                     button_name(state), esp_err_to_name(err));
        }
    }

    if (needs_client_callbacks) {
        ensure_client_callbacks();
    }

    return primary_handle;
}

} // namespace device_modules::button
