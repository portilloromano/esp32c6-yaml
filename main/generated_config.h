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

const cluster_config clusters_ep_1[] = {
    {.name = "on_off"},
    {.name = "level_control"},
};

const endpoint_config endpoints[] = {
    {
        .id = 1,
        .device_type = "light",
        .num_clusters = 2,
        .clusters = clusters_ep_1
    },
};

const uint8_t num_endpoints = sizeof(endpoints) / sizeof(endpoint_config);

} // namespace generated_config
