# Register the component with IDF build system
idf_component_register(
    SRC_DIRS "."
    PRIV_INCLUDE_DIRS "."
    REQUIRES
        driver
        led_indicator
        nvs_flash
        esp_matter
        ieee802154
)