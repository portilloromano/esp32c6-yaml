idf_component_register(
    SRC_DIRS "." "device_modules" "device_modules/common" "device_modules/light" "device_modules/switch"
    PRIV_INCLUDE_DIRS "." "device_modules" "${CMAKE_BINARY_DIR}"
    # La dependencia de esp_matter ya trae consigo las demás necesarias.
    # ieee802154 será añadido condicionalmente por el sistema de compilación de esp-matter
    # si se selecciona 'thread' en config.yaml.
    REQUIRES esp_matter
)

# Esta es la forma correcta de declarar la dependencia.
# El target 'generate_config' está definido en esp-matter y lee nuestro config.yaml
# para generar un archivo de cabecera y configuraciones de sdkconfig.
add_dependencies(${COMPONENT_TARGET} generate_config)
