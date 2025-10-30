import argparse
import os
import yaml

DEFAULT_CLUSTER_MAP = {
    "on_off_light": {"identify", "groups", "scenes_management", "on_off"},
    "dimmable_light": {"identify", "groups", "scenes_management", "on_off", "level_control"},
    "extended_color_light": {
        "identify",
        "groups",
        "scenes_management",
        "on_off",
        "level_control",
        "color_control",
    },
}

DEFAULT_FEATURE_MAP = {
    "on_off_light": {
        "on_off": {"lighting"},
    },
    "dimmable_light": {
        "on_off": {"lighting"},
        "level_control": {"on_off", "lighting"},
    },
    "extended_color_light": {
        "on_off": {"lighting"},
        "level_control": {"on_off", "lighting"},
        "color_control": {"color_temperature", "xy"},
    },
}

COLOR_MODE_MAP = {
    "kColorTemperature": "chip::app::Clusters::ColorControl::ColorMode::kColorTemperature",
    "kCurrentHueAndCurrentSaturation": "chip::app::Clusters::ColorControl::ColorMode::kCurrentHueAndCurrentSaturation",
    "kHueSaturation": "chip::app::Clusters::ColorControl::ColorMode::kHueSaturation",
    "kXY": "chip::app::Clusters::ColorControl::ColorMode::kXY",
    "kUndefined": "chip::app::Clusters::ColorControl::ColorMode::kUndefined",
}


def to_cpp_bool(value: bool) -> str:
    return "true" if bool(value) else "false"


def normalize_cluster_config(raw_value):
    """Return (explicit_enabled, data_dict) for a cluster entry."""
    explicit_enabled = None
    data = {}
    if isinstance(raw_value, dict):
        if "enabled" in raw_value:
            explicit_enabled = bool(raw_value["enabled"])
        data = {k: v for k, v in raw_value.items() if k != "enabled"}
    elif raw_value is None:
        explicit_enabled = True
    elif isinstance(raw_value, bool):
        explicit_enabled = raw_value
    elif raw_value is not None:
        explicit_enabled = True
    return explicit_enabled, data


def color_mode_expr(value: str | None, fallback: str) -> str:
    mode_key = value or fallback
    if mode_key not in COLOR_MODE_MAP:
        raise ValueError(f"Unsupported color_mode '{mode_key}'. Valid options: {', '.join(COLOR_MODE_MAP.keys())}")
    return f"static_cast<uint8_t>({COLOR_MODE_MAP[mode_key]})"


def feature_set(device_type: str, cluster_name: str, cluster_data: dict) -> set[str]:
    if "features" in cluster_data and cluster_data["features"] is not None:
        return {str(item) for item in cluster_data["features"]}
    return set(DEFAULT_FEATURE_MAP.get(device_type, {}).get(cluster_name, set()))


def parse_endpoint(endpoint: dict) -> dict:
    ep_id = endpoint["id"]
    device_type = endpoint.get("device_type", "on_off_light")
    clusters = endpoint.get("clusters", {}) or {}
    default_clusters = DEFAULT_CLUSTER_MAP.get(device_type, set())

    result = {
        "id": ep_id,
        "device_type": device_type,
    }

    def is_enabled(name: str, explicit: bool | None) -> bool:
        if explicit is not None:
            return explicit
        return name in clusters or name in default_clusters

    # Identify
    raw_identify = clusters.get("identify")
    identify_enabled, identify_data = normalize_cluster_config(raw_identify)
    identify_time = identify_data.get("identify_time", 0)
    identify_type = identify_data.get("identify_type", 0)
    result["identify"] = {
        "enabled": is_enabled("identify", identify_enabled),
        "identify_time": int(identify_time),
        "identify_type": int(identify_type),
    }

    # Groups
    raw_groups = clusters.get("groups")
    groups_enabled, _ = normalize_cluster_config(raw_groups)
    result["groups"] = {
        "enabled": is_enabled("groups", groups_enabled),
    }

    # Scenes Management
    raw_scenes = clusters.get("scenes_management")
    scenes_enabled, scenes_data = normalize_cluster_config(raw_scenes)
    scene_table_size = scenes_data.get("scene_table_size", 16)
    result["scenes_management"] = {
        "enabled": is_enabled("scenes_management", scenes_enabled),
        "scene_table_size": int(scene_table_size),
    }

    # On/Off
    raw_on_off = clusters.get("on_off")
    on_off_enabled, on_off_data = normalize_cluster_config(raw_on_off)
    on_value = on_off_data.get("on", on_off_data.get("on_off", False))
    on_off_features = feature_set(device_type, "on_off", on_off_data)
    on_value = on_off_data.get("state", on_off_data.get("on", on_off_data.get("on_off", False)))
    if isinstance(on_value, str):
        on_value = on_value.lower() in {"true", "yes", "1", "on"}
    result["on_off"] = {
        "enabled": is_enabled("on_off", on_off_enabled),
        "on": bool(on_value),
        "feature_lighting": "lighting" in on_off_features,
    }

    # Level Control
    raw_level = clusters.get("level_control")
    level_enabled, level_data = normalize_cluster_config(raw_level)
    level_features = feature_set(device_type, "level_control", level_data)
    level_config = {
        "enabled": is_enabled("level_control", level_enabled),
        "current_level": int(level_data.get("current_level", 0)),
        "options": int(level_data.get("options", 0)),
        "feature_on_off": "on_off" in level_features,
        "feature_lighting": "lighting" in level_features,
        "has_on_level": "on_level" in level_data and level_data["on_level"] is not None,
        "on_level": int(level_data.get("on_level", 0)),
    }
    result["level_control"] = level_config

    # Color Control
    raw_color = clusters.get("color_control")
    color_enabled, color_data = normalize_cluster_config(raw_color)
    color_features = feature_set(device_type, "color_control", color_data)
    color_mode = color_mode_expr(color_data.get("color_mode"), "kColorTemperature")
    enhanced_mode = color_mode_expr(color_data.get("enhanced_color_mode"), color_data.get("color_mode") or "kColorTemperature")
    result["color_control"] = {
        "enabled": is_enabled("color_control", color_enabled),
        "color_mode": color_mode,
        "enhanced_color_mode": enhanced_mode,
        "has_current_hue": "current_hue" in color_data and color_data["current_hue"] is not None,
        "current_hue": int(color_data.get("current_hue", 0)),
        "has_current_saturation": "current_saturation" in color_data and color_data["current_saturation"] is not None,
        "current_saturation": int(color_data.get("current_saturation", 0)),
        "has_color_temperature": "color_temperature_mireds" in color_data and color_data["color_temperature_mireds"] is not None,
        "color_temperature_mireds": int(color_data.get("color_temperature_mireds", 0)),
        "feature_color_temperature": "color_temperature" in color_features,
        "feature_xy": "xy" in color_features,
        "has_remaining_time": "remaining_time" in color_data and color_data["remaining_time"] is not None,
        "remaining_time": int(color_data.get("remaining_time", 0)),
    }

    return result


def write_generated_header(output_path: str, app_config: dict, endpoints: list[dict], app_priv_path: str) -> None:
    device_type = app_config.get("device_type", "light")
    device_name = app_config.get("device_name", "ESP32 Matter Device")
    button_config = app_config.get("button", {})
    led_strip_config = app_config.get("led_strip", {})

    # Read LED_STRIP_LED_COUNT from app_priv.h if it exists and is not 0 in config.yaml
    with open(output_path, "w", encoding="utf-8") as f:
        f.write("#pragma once\n\n")
        f.write("#include <stdint.h>\n")
        f.write("#include <esp_matter_cluster.h> // For ColorControl enums\n\n")
        f.write("// This file is generated automatically by config_generator.py. Do not edit.\n\n")

        # --- Top-level macros based on config presence ---
        button_count = 1 if button_config else 0 # Assume 1 button if config exists
        
        # Determine LED_STRIP_LED_COUNT from config.yaml, default to 0 if not specified
        led_strip_count = led_strip_config.get('led_count', 0)
        
        # If app_priv.h exists and defines LED_STRIP_LED_COUNT, use that if config.yaml doesn't specify
        # This is a fallback/compatibility measure.
        if led_strip_count == 0 and os.path.exists(app_priv_path):
            with open(app_priv_path, "r", encoding="utf-8") as app_priv_file:
                if "#define LED_STRIP_LED_COUNT 1" in app_priv_file.read():
                    led_strip_count = 1
        f.write(f"#define BUTTON_COUNT {button_count}\n")
        f.write(f"#define LED_STRIP_LED_COUNT {led_strip_config.get('led_count', 0)}\n\n")

        # --- Button Configuration Namespace ---
        if button_count > 0:
            f.write("namespace generated_config::button {\n")
            f.write(f"inline const int gpio = {button_config.get('gpio', -1)};\n")
            f.write(f"inline const int active_level = {button_config.get('active_level', 0)};\n")
            f.write(f"inline const int long_press_time_ms = {button_config.get('long_press_time_ms', 5000)};\n")
            f.write(f"inline const int short_press_timeout_ms = {button_config.get('short_press_timeout_ms', 2000)};\n")
            f.write(f"inline const int identify_trigger_count = {button_config.get('identify_trigger_count', 5)};\n")
            f.write(f"inline const int identify_time_s = {button_config.get('identify_time_s', 10)};\n")
            f.write("} // namespace generated_config::button\n\n")

        # --- LED Strip Configuration Namespace ---
        if led_strip_count > 0:
            f.write("namespace generated_config::led_strip {\n")
            f.write(f"inline const int rmt_gpio = {led_strip_config.get('rmt_gpio', -1)};\n")
            f.write(f"inline const char* type = \"{led_strip_config.get('type', 'ws2812')}\";\n")
            f.write("} // namespace generated_config::led_strip\n\n")

        f.write("namespace generated_config {\n\n")
        f.write(f'inline const char *device_type = "{device_type}";\n')
        f.write(f'inline const char *device_name = "{device_name}";\n\n')

        f.write("struct identify_cluster_config {\n")
        f.write("    bool enabled;\n")
        f.write("    uint16_t identify_time;\n")
        f.write("    uint8_t identify_type;\n")
        f.write("};\n\n")

        f.write("struct groups_cluster_config {\n")
        f.write("    bool enabled;\n")
        f.write("};\n\n")

        f.write("struct scenes_management_cluster_config {\n")
        f.write("    bool enabled;\n")
        f.write("    uint16_t scene_table_size;\n")
        f.write("};\n\n")

        f.write("struct on_off_cluster_config {\n")
        f.write("    bool enabled;\n")
        f.write("    bool on;\n")
        f.write("    bool feature_lighting;\n")
        f.write("};\n\n")

        f.write("struct level_control_cluster_config {\n")
        f.write("    bool enabled;\n")
        f.write("    uint8_t current_level;\n")
        f.write("    uint8_t options;\n")
        f.write("    bool feature_on_off;\n")
        f.write("    bool feature_lighting;\n")
        f.write("    bool has_on_level;\n")
        f.write("    uint8_t on_level;\n")
        f.write("};\n\n")

        f.write("struct color_control_cluster_config {\n")
        f.write("    bool enabled;\n")
        f.write("    uint8_t color_mode;\n")
        f.write("    uint8_t enhanced_color_mode;\n")
        f.write("    bool has_current_hue;\n")
        f.write("    uint8_t current_hue;\n")
        f.write("    bool has_current_saturation;\n")
        f.write("    uint8_t current_saturation;\n")
        f.write("    bool has_color_temperature;\n")
        f.write("    uint16_t color_temperature_mireds;\n")
        f.write("    bool feature_color_temperature;\n")
        f.write("    bool feature_xy;\n")
        f.write("    bool has_remaining_time;\n")
        f.write("    uint16_t remaining_time;\n")
        f.write("};\n\n")

        f.write("struct endpoint_config {\n")
        f.write("    uint16_t id;\n")
        f.write("    const char *device_type;\n")
        f.write("    identify_cluster_config identify;\n")
        f.write("    groups_cluster_config groups;\n")
        f.write("    scenes_management_cluster_config scenes_management;\n")
        f.write("    on_off_cluster_config on_off;\n")
        f.write("    level_control_cluster_config level_control;\n")
        f.write("    color_control_cluster_config color_control;\n")
        f.write("};\n\n")

        f.write("inline const endpoint_config endpoints[] = {\n")
        for idx, ep in enumerate(endpoints):
            f.write("    {\n")
            f.write(f"        .id = {ep['id']},\n")
            f.write(f"        .device_type = \"{ep['device_type']}\",\n")

            ident = ep["identify"]
            f.write("        .identify = {\n")
            f.write(f"            .enabled = {to_cpp_bool(ident['enabled'])},\n")
            f.write(f"            .identify_time = {ident['identify_time']},\n")
            f.write(f"            .identify_type = {ident['identify_type']},\n")
            f.write("        },\n")

            groups = ep["groups"]
            f.write("        .groups = {\n")
            f.write(f"            .enabled = {to_cpp_bool(groups['enabled'])},\n")
            f.write("        },\n")

            scenes = ep["scenes_management"]
            f.write("        .scenes_management = {\n")
            f.write(f"            .enabled = {to_cpp_bool(scenes['enabled'])},\n")
            f.write(f"            .scene_table_size = {scenes['scene_table_size']},\n")
            f.write("        },\n")

            on_off = ep["on_off"]
            f.write("        .on_off = {\n")
            f.write(f"            .enabled = {to_cpp_bool(on_off['enabled'])},\n")
            f.write(f"            .on = {to_cpp_bool(on_off['on'])},\n")
            f.write(f"            .feature_lighting = {to_cpp_bool(on_off['feature_lighting'])},\n")
            f.write("        },\n")

            level = ep["level_control"]
            f.write("        .level_control = {\n")
            f.write(f"            .enabled = {to_cpp_bool(level['enabled'])},\n")
            f.write(f"            .current_level = {level['current_level']},\n")
            f.write(f"            .options = {level['options']},\n")
            f.write(f"            .feature_on_off = {to_cpp_bool(level['feature_on_off'])},\n")
            f.write(f"            .feature_lighting = {to_cpp_bool(level['feature_lighting'])},\n")
            f.write(f"            .has_on_level = {to_cpp_bool(level['has_on_level'])},\n")
            f.write(f"            .on_level = {level['on_level']},\n")
            f.write("        },\n")

            color = ep["color_control"]
            f.write("        .color_control = {\n")
            f.write(f"            .enabled = {to_cpp_bool(color['enabled'])},\n")
            f.write(f"            .color_mode = {color['color_mode']},\n")
            f.write(f"            .enhanced_color_mode = {color['enhanced_color_mode']},\n")
            f.write(f"            .has_current_hue = {to_cpp_bool(color['has_current_hue'])},\n")
            f.write(f"            .current_hue = {color['current_hue']},\n")
            f.write(f"            .has_current_saturation = {to_cpp_bool(color['has_current_saturation'])},\n")
            f.write(f"            .current_saturation = {color['current_saturation']},\n")
            f.write(f"            .has_color_temperature = {to_cpp_bool(color['has_color_temperature'])},\n")
            f.write(f"            .color_temperature_mireds = {color['color_temperature_mireds']},\n")
            f.write(f"            .feature_color_temperature = {to_cpp_bool(color['feature_color_temperature'])},\n")
            f.write(f"            .feature_xy = {to_cpp_bool(color['feature_xy'])},\n")
            f.write(f"            .has_remaining_time = {to_cpp_bool(color['has_remaining_time'])},\n")
            f.write(f"            .remaining_time = {color['remaining_time']},\n")
            f.write("        },\n")

            f.write("    }")
            if idx < len(endpoints) - 1:
                f.write(",")
            f.write("\n")

        f.write("};\n\n")
        f.write("inline const uint8_t num_endpoints = sizeof(endpoints) / sizeof(endpoint_config);\n\n")
        f.write("} // namespace generated_config\n")


def main():
    parser = argparse.ArgumentParser(description="Generate C++ header from YAML configuration.")
    parser.add_argument("config_file", help="Path to the input YAML config file.")
    parser.add_argument("output_header", help="Path to the output C++ header file.")
    args = parser.parse_args()

    with open(args.config_file, "r", encoding="utf-8") as cfg_file:
        config = yaml.safe_load(cfg_file)

    app_info = config.get("app", {})
    endpoints_yaml = app_info.get("endpoints", [])
    parsed_endpoints = [parse_endpoint(ep) for ep in endpoints_yaml]

    write_generated_header(args.output_header, app_info, parsed_endpoints, os.path.join(os.path.dirname(args.output_header), "app_priv.h"))
    print(f"Generated {args.output_header} from {args.config_file}")


if __name__ == "__main__":
    main()
