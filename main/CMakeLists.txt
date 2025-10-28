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