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

    header_content = """
#pragma once

#include <stdint.h>

// This file is generated automatically by config_generator.py. Do not edit.

namespace generated_config {

struct cluster_config {
    const char *name;
};

struct endpoint_config {
    uint16_t id;
    const char *device_type;
    int num_clusters;
    const cluster_config *clusters;
};

"""

    endpoint_structs = []
    cluster_arrays = []

    for i, endpoint in enumerate(config.get('endpoints', [])):
        cluster_array_name = f"clusters_ep_{endpoint['id']}"
        clusters = endpoint.get('clusters', [])
        
        cluster_defs = []
        for cluster in clusters:
            cluster_defs.append(f'        {{.name = "{cluster["name"]}"}}')

        cluster_arrays.append(f"const cluster_config {cluster_array_name}[] = {{\n" + ",\n".join(cluster_defs) + "\n};")

        endpoint_structs.append(
            f"    {{\n" \
            f"        .id = {endpoint['id']},\n" \
            f"        .device_type = \"{endpoint['device_type']}\",\n" \
            f"        .num_clusters = {len(clusters)},\n" \
            f"        .clusters = {cluster_array_name}\n" \
            f"    }}"
        )

    header_content += "\n".join(cluster_arrays)
    header_content += "\n\nconst endpoint_config endpoints[] = {\n" + ",\n".join(endpoint_structs) + "\n};\n"
    header_content += "\nconst uint8_t num_endpoints = sizeof(endpoints) / sizeof(endpoint_config);\n"
    header_content += "\n} // namespace generated_config\n"

    with open(args.output_header, 'w') as f:
        f.write(header_content)

    print(f"Generated {args.output_header} from {args.config_file}")

if __name__ == '__main__':
    main()
