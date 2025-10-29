import yaml
import os
import argparse

def main():
    parser = argparse.ArgumentParser(description='Generate C++ header from YAML configuration.')
    parser.add_argument('config_file', help='Path to the input YAML config file.')
    parser.add_argument('output_header', help='Path to the output C++ header file.')
    args = parser.parse_args()

    with open(args.config_file, 'r') as f:
        config = yaml.safe_load(f)

    device_info = config.get('device', {})
    device_type = device_info.get('type', 'light')
    device_name = device_info.get('name', 'ESP32 Matter Device')

    with open(args.output_header, 'w') as f:
        f.write("#pragma once\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write("// This file is generated automatically by config_generator.py. Do not edit.\n\n")
        f.write("namespace generated_config {\n\n")
        f.write(f'const char *device_type = "{device_type}";\n')
        f.write(f'const char *device_name = "{device_name}";\n\n')
        f.write("struct endpoint_config {\n    uint16_t id;\n    const char *device_type;\n};\n\n")
        
        f.write("const endpoint_config endpoints[] = {\n")
        
        endpoints = config.get('endpoints', [])
        for i, endpoint in enumerate(endpoints):
            f.write(f'    {{ .id = {endpoint["id"]}, .device_type = "{endpoint["device_type"]}" }}')
            if i < len(endpoints) - 1:
                f.write(",")
            f.write("\n")

        f.write("};\n\n")
        f.write("const uint8_t num_endpoints = sizeof(endpoints) / sizeof(endpoint_config);\n\n")
        f.write("} // namespace generated_config\n")

    print(f"Generated {args.output_header} from {args.config_file}")

if __name__ == '__main__':
    main()
