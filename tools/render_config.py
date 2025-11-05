import argparse
import os
import shutil
from typing import Any

import yaml

SDKCONFIG_TEMPLATE_MAP = {
    "wifi": "sdkconfig.defaults_wifi",
    "thread": "sdkconfig.defaults_thread",
    "wifi_thread": "sdkconfig.defaults_wifi_thread",
}

PARTITION_TEMPLATE_MAP = {
    "4MB": "partitions.csv_4MB",
    "8MB": "partitions.csv_8MB",
    "16MB": "partitions.csv_16MB",
}


def optional_bool_literal(value: bool | None) -> str:
    if value is None:
        return "{false, false}"
    return "{true, " + ("true" if value else "false") + "}"


def optional_int_literal(value: int | None) -> str:
    if value is None:
        return "{false, 0}"
    return f"{{true, {value}}}"


def optional_string_literal(value: str | None) -> str:
    if value is None:
        return "{false, nullptr}"
    return f'{{true, "{value}"}}'


def cpp_string_literal(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def emit_header(output_path: str, data: dict[str, Any]) -> None:
    device_type = data.get("device_type", "light")
    device_name = data.get("device_name", "ESP32 Matter Device")
    network = data.get("network") or {}
    connectivity = str(network.get("connectivity", "wifi")).lower()
    flash = data.get("flash") or {}
    flash_size = str(flash.get("size", "4MB")).upper()
    buttons = data.get("buttons") or []
    led_strip = data.get("led_strip")
    endpoints = data.get("endpoints") or []

    button_count = len(buttons)
    led_strip_count = int(led_strip.get("led_count", 0)) if led_strip else 0

    feature_arrays: list[tuple[str, list[str]]] = []
    normalized_endpoints = []
    for idx, endpoint in enumerate(endpoints):
        endpoint_copy = dict(endpoint)
        for cluster_name in ("on_off", "level_control", "color_control"):
            cluster = dict(endpoint_copy.get(cluster_name, {}))
            features = cluster.get("features") or []
            if features:
                array_name = f"kEndpoint{idx}_{cluster_name}_features"
                feature_arrays.append((array_name, features))
                cluster["features_array"] = array_name
            else:
                cluster["features_array"] = None
            cluster["features_count"] = len(features)
            endpoint_copy[cluster_name] = cluster
        normalized_endpoints.append(endpoint_copy)

    with open(output_path, "w", encoding="utf-8") as f:
        f.write("#pragma once\n\n")
        f.write("#include <stddef.h>\n")
        f.write("#include <stdint.h>\n\n")
        f.write("// This file is generated automatically. Do not edit.\n\n")

        has_thread = connectivity in {"thread", "wifi_thread"}
        f.write(f"#define APP_NETWORK_CONNECTIVITY_THREAD {1 if has_thread else 0}\n")
        f.write(f"#define BUTTON_COUNT {button_count}\n")
        f.write(f"#define LED_STRIP_LED_COUNT {led_strip_count}\n")
        f.write(f"#define FLASH_SIZE_MB {flash_size[:-2]}\n\n")

        f.write("namespace generated_config::button {\n")
        f.write("struct config_t {\n")
        f.write("    const char *id;\n")
        f.write("    int gpio;\n")
        f.write("    int active_level;\n")
        f.write("    int long_press_time_ms;\n")
        f.write("    int short_press_timeout_ms;\n")
        f.write("    int identify_trigger_count;\n")
        f.write("    int identify_time_s;\n")
        f.write("    const char *mode;\n")
        f.write("    const char *action_cluster;\n")
        f.write("    const char *action_command;\n")
        f.write("    int action_identify_time_s;\n")
        f.write("    uint16_t binding_endpoint;\n")
        f.write("    uint16_t target_endpoint;\n")
        f.write("    const char *driver;\n")
        f.write("};\n\n")
        f.write(f"inline constexpr size_t count = {button_count};\n")
        f.write("inline constexpr config_t configs[] = {\n")
        for button in buttons:
            id_literal = f'"{cpp_string_literal(button["id"])}"' if button.get("id") else "nullptr"
            mode_literal = f'"{cpp_string_literal(button.get("mode", "remote"))}"'
            cluster_literal = f'"{cpp_string_literal(button.get("action_cluster", "on_off"))}"'
            command_literal = f'"{cpp_string_literal(button.get("action_command", "toggle"))}"'
            driver_literal = (
                f'"{cpp_string_literal(button["driver"])}"' if button.get("driver") else "nullptr"
            )
            f.write("    {\n")
            f.write(f"        .id = {id_literal},\n")
            f.write(f"        .gpio = {button.get('gpio', -1)},\n")
            f.write(f"        .active_level = {button.get('active_level', 0)},\n")
            f.write(f"        .long_press_time_ms = {button.get('long_press_time_ms', 5000)},\n")
            f.write(f"        .short_press_timeout_ms = {button.get('short_press_timeout_ms', 2000)},\n")
            f.write(f"        .identify_trigger_count = {button.get('identify_trigger_count', 5)},\n")
            f.write(f"        .identify_time_s = {button.get('identify_time_s', 10)},\n")
            f.write(f"        .mode = {mode_literal},\n")
            f.write(f"        .action_cluster = {cluster_literal},\n")
            f.write(f"        .action_command = {command_literal},\n")
            f.write(f"        .action_identify_time_s = {button.get('action_identify_time_s', 10)},\n")
            f.write(
                f"        .binding_endpoint = static_cast<uint16_t>({button.get('binding_endpoint', 0)}),\n"
            )
            f.write(
                f"        .target_endpoint = static_cast<uint16_t>({button.get('target_endpoint', 0)}),\n"
            )
            f.write(f"        .driver = {driver_literal},\n")
            f.write("    },\n")
        f.write("};\n")
        f.write("} // namespace generated_config::button\n\n")

        if led_strip_count > 0 and led_strip:
            f.write("namespace generated_config::led_strip {\n")
            f.write(f"inline constexpr int rmt_gpio = {int(led_strip.get('rmt_gpio', -1))};\n")
            led_type = led_strip.get("type", "ws2812")
            f.write(f'inline constexpr const char *type = "{cpp_string_literal(str(led_type))}";\n')
            f.write("} // namespace generated_config::led_strip\n\n")

        f.write("namespace generated_config {\n\n")
        f.write(f'inline constexpr const char *device_type = "{cpp_string_literal(device_type)}";\n')
        f.write(f'inline constexpr const char *device_name = "{cpp_string_literal(device_name)}";\n\n')

        f.write("struct optional_bool {\n    bool has_value;\n    bool value;\n};\n\n")
        f.write("struct optional_int {\n    bool has_value;\n    int32_t value;\n};\n\n")
        f.write("struct optional_string {\n    bool has_value;\n    const char *value;\n};\n\n")
        f.write("struct string_list {\n    size_t count;\n    const char *const *items;\n};\n\n")

        f.write("struct identify_cluster_raw {\n    bool present;\n    optional_bool enabled;\n    optional_int identify_time;\n    optional_int identify_type;\n};\n\n")
        f.write("struct groups_cluster_raw {\n    bool present;\n    optional_bool enabled;\n};\n\n")
        f.write("struct scenes_management_cluster_raw {\n    bool present;\n    optional_bool enabled;\n    optional_int scene_table_size;\n};\n\n")
        f.write("struct on_off_cluster_raw {\n    bool present;\n    optional_bool enabled;\n    optional_bool state;\n    string_list features;\n};\n\n")
        f.write("struct level_control_cluster_raw {\n    bool present;\n    optional_bool enabled;\n    optional_int current_level;\n    optional_int options;\n    optional_int on_level;\n    string_list features;\n};\n\n")
        f.write("struct color_control_cluster_raw {\n    bool present;\n    optional_bool enabled;\n    optional_string color_mode;\n    optional_string enhanced_color_mode;\n    optional_int current_hue;\n    optional_int current_saturation;\n    optional_int color_temperature_mireds;\n    optional_int remaining_time;\n    string_list features;\n};\n\n")
        f.write("struct endpoint_raw {\n    uint16_t id;\n    const char *device_type;\n    identify_cluster_raw identify;\n    groups_cluster_raw groups;\n    scenes_management_cluster_raw scenes_management;\n    on_off_cluster_raw on_off;\n    level_control_cluster_raw level_control;\n    color_control_cluster_raw color_control;\n};\n\n")

        for name, values in feature_arrays:
            items = ", ".join(f'"{cpp_string_literal(v)}"' for v in values)
            f.write(f"inline constexpr const char *{name}[] = {{{items}}};\n")
        if feature_arrays:
            f.write("\n")

        f.write("inline constexpr endpoint_raw endpoints[] = {\n")
        for idx, endpoint in enumerate(normalized_endpoints):
            f.write("    {\n")
            f.write(f"        .id = {endpoint['id']},\n")
            f.write(f"        .device_type = \"{cpp_string_literal(endpoint['device_type'])}\",\n")
            for cluster_name in ("identify", "groups", "scenes_management", "on_off", "level_control", "color_control"):
                cluster = endpoint[cluster_name]
                f.write(f"        .{cluster_name} = {{\n")
                if cluster_name == "identify":
                    f.write(f"            .present = {'true' if cluster['present'] else 'false'},\n")
                    f.write(f"            .enabled = {optional_bool_literal(cluster['enabled'])},\n")
                    f.write(f"            .identify_time = {optional_int_literal(cluster['identify_time'])},\n")
                    f.write(f"            .identify_type = {optional_int_literal(cluster['identify_type'])},\n")
                elif cluster_name == "groups":
                    f.write(f"            .present = {'true' if cluster['present'] else 'false'},\n")
                    f.write(f"            .enabled = {optional_bool_literal(cluster['enabled'])},\n")
                elif cluster_name == "scenes_management":
                    f.write(f"            .present = {'true' if cluster['present'] else 'false'},\n")
                    f.write(f"            .enabled = {optional_bool_literal(cluster['enabled'])},\n")
                    f.write(f"            .scene_table_size = {optional_int_literal(cluster['scene_table_size'])},\n")
                else:
                    f.write(f"            .present = {'true' if cluster['present'] else 'false'},\n")
                    f.write(f"            .enabled = {optional_bool_literal(cluster['enabled'])},\n")
                    if cluster_name == "on_off":
                        f.write(f"            .state = {optional_bool_literal(cluster['state'])},\n")
                    elif cluster_name == "level_control":
                        f.write(f"            .current_level = {optional_int_literal(cluster['current_level'])},\n")
                        f.write(f"            .options = {optional_int_literal(cluster['options'])},\n")
                        f.write(f"            .on_level = {optional_int_literal(cluster['on_level'])},\n")
                    elif cluster_name == "color_control":
                        f.write(f"            .color_mode = {optional_string_literal(cluster['color_mode'])},\n")
                        f.write(f"            .enhanced_color_mode = {optional_string_literal(cluster['enhanced_color_mode'])},\n")
                        f.write(f"            .current_hue = {optional_int_literal(cluster['current_hue'])},\n")
                        f.write(f"            .current_saturation = {optional_int_literal(cluster['current_saturation'])},\n")
                        f.write(f"            .color_temperature_mireds = {optional_int_literal(cluster['color_temperature_mireds'])},\n")
                        f.write(f"            .remaining_time = {optional_int_literal(cluster['remaining_time'])},\n")
                    features_ref = cluster.get("features_array") or "nullptr"
                    features_count = cluster.get("features_count", 0)
                    f.write(f"            .features = {{ {features_count}, {features_ref} }},\n")
                f.write("        },\n")
            f.write("    }")
            if idx < len(normalized_endpoints) - 1:
                f.write(",")
            f.write("\n")
        f.write("};\n\n")

        f.write("inline constexpr uint8_t num_endpoints = sizeof(endpoints) / sizeof(endpoint_raw);\n\n")
        f.write("} // namespace generated_config\n")


def find_template(project_root: str, script_dir: str, template_name: str) -> str:
    candidates = [
        os.path.join(project_root, "templates"),
        os.path.join(script_dir, "templates"),
        script_dir,
    ]
    for directory in candidates:
        candidate = os.path.join(directory, template_name)
        if os.path.exists(candidate):
            return candidate
    searched = ", ".join(os.path.join(directory, template_name) for directory in candidates)
    raise FileNotFoundError(
        f"Missing sdkconfig template '{template_name}'. Searched in: {searched}"
    )


def write_sdkconfig_defaults(project_root: str, script_dir: str, connectivity: str) -> None:
    template_name = SDKCONFIG_TEMPLATE_MAP.get(connectivity)
    if template_name is None:
        valid_options = ", ".join(sorted(SDKCONFIG_TEMPLATE_MAP))
        raise ValueError(f"Unsupported connectivity '{connectivity}'. Expected one of: {valid_options}.")

    template_path = find_template(project_root, script_dir, template_name)
    defaults_path = os.path.join(project_root, "sdkconfig.defaults")
    shutil.copyfile(template_path, defaults_path)


def copy_partition_table(project_root: str, script_dir: str, flash_size: str) -> None:
    template_name = PARTITION_TEMPLATE_MAP.get(flash_size)
    if template_name is None:
        valid = ", ".join(sorted(k for k, v in PARTITION_TEMPLATE_MAP.items() if v))
        raise ValueError(f"Unsupported flash_size '{flash_size}'. Expected one of: {valid}.")

    template_path = find_template(project_root, script_dir, template_name)
    target_path = os.path.join(project_root, "partitions.csv")
    shutil.copyfile(template_path, target_path)

    sanitized_lines: list[str] = []
    with open(target_path, "r", encoding="utf-8") as part_file:
        for line in part_file.readlines():
            if "#" in line and not line.lstrip().startswith("#"):
                comment_idx = line.index("#")
                line = line[:comment_idx].rstrip() + "\n"
            sanitized_lines.append(line)

    with open(target_path, "w", encoding="utf-8") as part_file:
        part_file.writelines(sanitized_lines)


def _format_kconfig_value(value: Any) -> str:
    if isinstance(value, bool):
        return "y" if value else "n"
    return str(value)


def apply_kconfig_overrides(config_path: str, overrides: dict[str, Any]) -> None:
    if not os.path.exists(config_path):
        return

    with open(config_path, "r", encoding="utf-8") as cfg:
        lines = cfg.readlines()

    overrides = {key: _format_kconfig_value(val) for key, val in overrides.items()}
    handled = {key: False for key in overrides}

    def set_line(key: str, value: str) -> str:
        if value in ("y", "n"):
            return f"{'CONFIG_' + key}=y\n" if value == "y" else f"# CONFIG_{key} is not set\n"
        return f"CONFIG_{key}={value}\n"

    new_lines: list[str] = []
    for line in lines:
        updated = False
        for key, value in overrides.items():
            if line.startswith(f"CONFIG_{key}=") or line.startswith(f"# CONFIG_{key} is not set"):
                new_lines.append(set_line(key, value))
                handled[key] = True
                updated = True
                break
        if not updated:
            new_lines.append(line)

    for key, value in overrides.items():
        if not handled[key]:
            new_lines.append(set_line(key, value))

    with open(config_path, "w", encoding="utf-8") as cfg:
        cfg.writelines(new_lines)


def main() -> None:
    parser = argparse.ArgumentParser(description="Render generated_config.h and sdkconfig defaults.")
    parser.add_argument("normalized_config", help="Path to the normalized YAML produced by parse_config.py.")
    parser.add_argument("output_header", help="Path to the output C++ header file.")
    parser.add_argument("project_root", help="Project root used to locate sdkconfig templates.")
    args = parser.parse_args()

    with open(args.normalized_config, "r", encoding="utf-8") as normalized_file:
        data = yaml.safe_load(normalized_file) or {}

    os.makedirs(os.path.dirname(os.path.abspath(args.output_header)), exist_ok=True)
    emit_header(args.output_header, data)

    connectivity = (data.get("network") or {}).get("connectivity", "wifi")
    script_dir = os.path.dirname(os.path.abspath(__file__))
    write_sdkconfig_defaults(args.project_root, script_dir, connectivity)

    flash_size = str((data.get("flash") or {}).get("size", "4MB")).upper()
    copy_partition_table(args.project_root, script_dir, flash_size)

    sdkconfig_path = os.path.join(args.project_root, "sdkconfig")
    if connectivity == "thread":
        overrides = {
            "OPENTHREAD_ENABLED": True,
            "ESP_MATTER_ENABLE_OPENTHREAD": True,
            "ENABLE_WIFI_STATION": False,
            "ENABLE_WIFI_AP": False,
        }
    elif connectivity == "wifi":
        overrides = {
            "OPENTHREAD_ENABLED": False,
            "ESP_MATTER_ENABLE_OPENTHREAD": False,
            "ENABLE_WIFI_STATION": True,
            "ENABLE_WIFI_AP": False,
        }
    elif connectivity == "wifi_thread":
        overrides = {
            "OPENTHREAD_ENABLED": True,
            "ESP_MATTER_ENABLE_OPENTHREAD": True,
            "ENABLE_WIFI_STATION": True,
            "ENABLE_WIFI_AP": False,
        }
    else:
        valid_options = ", ".join(sorted(SDKCONFIG_TEMPLATE_MAP))
        raise ValueError(f"Unsupported connectivity '{connectivity}'. Expected one of: {valid_options}.")

    apply_kconfig_overrides(sdkconfig_path, overrides)
    print(f"Generated {args.output_header} from {args.normalized_config}")


if __name__ == "__main__":
    main()
