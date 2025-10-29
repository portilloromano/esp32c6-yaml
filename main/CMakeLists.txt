idf_component_register(
    SRC_DIRS "."
    PRIV_INCLUDE_DIRS "." "${CMAKE_BINARY_DIR}"
    REQUIRES
        driver
        led_indicator
        nvs_flash
        esp_matter
        ieee802154
)

# This is the correct way to declare the dependency. 
# The generate_config target is defined in tools/build.cmake
add_dependencies(${COMPONENT_TARGET} generate_config)
