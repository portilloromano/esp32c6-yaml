import argparse
import os
from typing import Any, Iterable

import yaml

TRUE_STRINGS = {"true", "yes", "1", "on"}
FALSE_STRINGS = {"false", "no", "0", "off"}


def parse_bool(value: Any) -> bool | None:
    if value is None:
        return None
    if isinstance(value, bool):
        return value
    if isinstance(value, str):
        lowered = value.strip().lower()
        if lowered in TRUE_STRINGS:
            return True
        if lowered in FALSE_STRINGS:
            return False
    return bool(value)


def parse_int(value: Any) -> int | None:
    if value is None:
        return None
    if isinstance(value, bool):
        return 1 if value else 0
    try:
        return int(value)
    except (ValueError, TypeError):
        return None


def parse_string(value: Any) -> str | None:
    if value is None:
        return None
    return str(value)


def parse_string_list(values: Any) -> list[str]:
    if not values:
        return []
    if isinstance(values, str):
        return [values]
    if isinstance(values, Iterable):
        result: list[str] = []
        for item in values:
            if item is not None:
                result.append(str(item))
        return result
    return []


def cluster_entry(clusters: dict[str, Any], key: str) -> tuple[bool, bool | None, dict[str, Any]]:
    if key not in clusters:
        return False, None, {}
    raw = clusters[key]
    enabled = None
    data: dict[str, Any] = {}
    if isinstance(raw, dict):
        enabled = parse_bool(raw.get("enabled"))
        data = {k: v for k, v in raw.items() if k != "enabled"}
    elif isinstance(raw, bool):
        enabled = raw
    elif raw is None:
        enabled = None
    else:
        enabled = parse_bool(raw)
    return True, enabled, data


def extract_on_off_state(data: dict[str, Any]) -> bool | None:
    for key in ("state", "on", "on_off"):
        if key in data:
            return parse_bool(data[key])
    return None


def parse_button_entry(button: dict[str, Any], default_mode: str) -> dict[str, Any]:
    if not isinstance(button, dict):
        raise ValueError("Each button entry must be a mapping.")

    gpio = parse_int(button.get("gpio"))
    if gpio is None:
        raise ValueError("Button definition is missing a valid 'gpio' value.")

    active_level = parse_int(button.get("active_level"))
    if active_level is None:
        active_level = 0

    long_press_time_ms = parse_int(button.get("long_press_time_ms"))
    if long_press_time_ms is None:
        long_press_time_ms = 5000

    short_press_timeout_ms = parse_int(button.get("short_press_timeout_ms"))
    if short_press_timeout_ms is None:
        short_press_timeout_ms = 2000

    identify_trigger_count = parse_int(button.get("identify_trigger_count"))
    if identify_trigger_count is None:
        identify_trigger_count = 5

    identify_time_s = parse_int(button.get("identify_time_s"))
    if identify_time_s is None:
        identify_time_s = 10

    mode = parse_string(button.get("mode"))
    mode = mode.lower() if mode else default_mode

    action = button.get("action", {}) or {}
    action_cluster = parse_string(action.get("cluster")) or "on_off"
    action_cluster = action_cluster.lower()

    action_command = parse_string(action.get("command"))
    if not action_command:
        action_command = "identify" if action_cluster == "identify" else "toggle"
    action_command = action_command.lower()

    action_identify_time_s = parse_int(action.get("identify_time_s"))
    if action_identify_time_s is None:
        action_identify_time_s = identify_time_s

    binding_endpoint = parse_int(button.get("binding_endpoint"))
    if binding_endpoint is None:
        binding_endpoint = 0

    target_endpoint = parse_int(action.get("target_endpoint"))
    if target_endpoint is None:
        target_endpoint = parse_int(button.get("target_endpoint"))
    if target_endpoint is None:
        target_endpoint = 0

    driver = parse_string(action.get("driver"))
    if not driver:
        driver = parse_string(button.get("driver"))

    return {
        "id": parse_string(button.get("id")),
        "gpio": int(gpio),
        "active_level": int(active_level),
        "long_press_time_ms": int(long_press_time_ms),
        "short_press_timeout_ms": int(short_press_timeout_ms),
        "identify_trigger_count": int(identify_trigger_count),
        "identify_time_s": int(identify_time_s),
        "mode": mode,
        "action_cluster": action_cluster,
        "action_command": action_command,
        "action_identify_time_s": int(action_identify_time_s),
        "binding_endpoint": int(binding_endpoint),
        "target_endpoint": int(target_endpoint),
        "driver": parse_string(driver),
    }


def parse_endpoint_entry(endpoint: dict[str, Any]) -> dict[str, Any]:
    clusters = endpoint.get("clusters", {}) or {}
    identify_present, identify_enabled, identify_data = cluster_entry(clusters, "identify")
    groups_present, groups_enabled, groups_data = cluster_entry(clusters, "groups")
    scenes_present, scenes_enabled, scenes_data = cluster_entry(clusters, "scenes_management")
    on_off_present, on_off_enabled, on_off_data = cluster_entry(clusters, "on_off")
    level_present, level_enabled, level_data = cluster_entry(clusters, "level_control")
    color_present, color_enabled, color_data = cluster_entry(clusters, "color_control")

    return {
        "id": int(endpoint["id"]),
        "device_type": endpoint.get("device_type", "on_off_light"),
        "identify": {
            "present": identify_present,
            "enabled": identify_enabled,
            "identify_time": parse_int(identify_data.get("identify_time")),
            "identify_type": parse_int(identify_data.get("identify_type")),
        },
        "groups": {
            "present": groups_present,
            "enabled": groups_enabled,
        },
        "scenes_management": {
            "present": scenes_present,
            "enabled": scenes_enabled,
            "scene_table_size": parse_int(scenes_data.get("scene_table_size")),
        },
        "on_off": {
            "present": on_off_present,
            "enabled": on_off_enabled,
            "state": extract_on_off_state(on_off_data),
            "features": parse_string_list(on_off_data.get("features")),
        },
        "level_control": {
            "present": level_present,
            "enabled": level_enabled,
            "current_level": parse_int(level_data.get("current_level")),
            "options": parse_int(level_data.get("options")),
            "on_level": parse_int(level_data.get("on_level")),
            "features": parse_string_list(level_data.get("features")),
        },
        "color_control": {
            "present": color_present,
            "enabled": color_enabled,
            "color_mode": parse_string(color_data.get("color_mode")),
            "enhanced_color_mode": parse_string(color_data.get("enhanced_color_mode")),
            "current_hue": parse_int(color_data.get("current_hue")),
            "current_saturation": parse_int(color_data.get("current_saturation")),
            "color_temperature_mireds": parse_int(color_data.get("color_temperature_mireds")),
            "remaining_time": parse_int(color_data.get("remaining_time")),
            "features": parse_string_list(color_data.get("features")),
        },
    }


def normalize_configuration(config: dict[str, Any]) -> dict[str, Any]:
    app_info = config.get("app", {}) if config else {}
    endpoints_yaml = app_info.get("endpoints", []) if app_info else []
    parsed_endpoints = [parse_endpoint_entry(ep) for ep in endpoints_yaml]

    buttons_yaml = app_info.get("buttons", []) if app_info else []
    if not buttons_yaml and app_info.get("button"):
        legacy_button = dict(app_info.get("button") or {})
        if "mode" not in legacy_button:
            legacy_button["mode"] = "remote" if app_info.get("device_type") == "switch" else "local"
        buttons_yaml = [legacy_button]

    device_type = app_info.get("device_type", "light")
    default_mode = "remote" if device_type == "switch" else "local"
    parsed_buttons = [parse_button_entry(btn, default_mode) for btn in buttons_yaml]

    led_strip_config = app_info.get("led_strip", {}) or {}
    network_config = app_info.get("network", {}) or {}
    connectivity = str(network_config.get("connectivity", "wifi")).lower()

    raw_flash_size = app_info.get("flash_size") or app_info.get("flash")
    flash_size_str = parse_string(raw_flash_size)
    if flash_size_str is None:
        flash_size_str = "4MB"
    flash_size_str = flash_size_str.strip().upper()
    if flash_size_str.isdigit():
        flash_size_str = f"{flash_size_str}MB"
    if not flash_size_str.endswith("MB"):
        raise ValueError("flash_size must be specified in megabytes, e.g., '4MB', '8MB'.")

    valid_flash_sizes = {"4MB", "8MB", "16MB"}
    if flash_size_str not in valid_flash_sizes:
        raise ValueError(
            f"Unsupported flash_size '{flash_size_str}'. Supported values: {', '.join(sorted(valid_flash_sizes))}."
        )

    return {
        "device_type": device_type,
        "device_name": app_info.get("device_name", "ESP32 Matter Device"),
        "network": {"connectivity": connectivity},
        "led_strip": {
            "led_count": parse_int(led_strip_config.get("led_count")) or 0,
            "rmt_gpio": parse_int(led_strip_config.get("rmt_gpio")) or -1,
            "type": parse_string(led_strip_config.get("type")) or "ws2812",
        } if led_strip_config else None,
        "buttons": parsed_buttons,
        "endpoints": parsed_endpoints,
        "flash": {"size": flash_size_str},
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="Normalize YAML configuration into an intermediate representation.")
    parser.add_argument("config_file", help="Path to the input YAML config file.")
    parser.add_argument("output_file", help="Path to the normalized YAML output.")
    args = parser.parse_args()

    with open(args.config_file, "r", encoding="utf-8") as cfg_file:
        raw_config = yaml.safe_load(cfg_file) or {}

    normalized = normalize_configuration(raw_config)

    os.makedirs(os.path.dirname(os.path.abspath(args.output_file)), exist_ok=True)
    with open(args.output_file, "w", encoding="utf-8") as out_file:
        yaml.safe_dump(normalized, out_file, sort_keys=False)

    print(f"Normalized configuration written to {args.output_file}")


if __name__ == "__main__":
    main()
