
# Install python requirements
set(PIP_REQUIREMENTS_FILE ${CMAKE_SOURCE_DIR}/tools/requirements.txt)
set(STAMP_FILE ${CMAKE_CURRENT_BINARY_DIR}/pip_requirements_installed.stamp)

add_custom_command(
    OUTPUT ${STAMP_FILE}
    COMMAND ${PYTHON} -m pip install -r ${PIP_REQUIREMENTS_FILE}
    COMMAND ${CMAKE_COMMAND} -E touch ${STAMP_FILE}
    DEPENDS ${PIP_REQUIREMENTS_FILE}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Installing python dependencies from ${PIP_REQUIREMENTS_FILE}"
    VERBATIM
)

add_custom_target(
    pip_requirements ALL
    DEPENDS ${STAMP_FILE}
)

# Generate intermediate parsed config and final C++ header
set(CONFIG_YAML ${CMAKE_SOURCE_DIR}/config.yaml)
set(GENERATED_HEADER ${CMAKE_BINARY_DIR}/generated_config.h)
set(PARSED_CONFIG ${CMAKE_BINARY_DIR}/parsed_config.yaml)

add_custom_command(
    OUTPUT ${PARSED_CONFIG}
    COMMAND ${PYTHON} ${CMAKE_SOURCE_DIR}/tools/parse_config.py ${CONFIG_YAML} ${PARSED_CONFIG}
    DEPENDS ${CONFIG_YAML} ${CMAKE_SOURCE_DIR}/tools/parse_config.py pip_requirements
    COMMENT "Parsing ${CONFIG_YAML}"
    VERBATIM
)

add_custom_command(
    OUTPUT ${GENERATED_HEADER}
    COMMAND ${PYTHON} ${CMAKE_SOURCE_DIR}/tools/render_config.py ${PARSED_CONFIG} ${GENERATED_HEADER} ${CMAKE_SOURCE_DIR}
    DEPENDS
        ${PARSED_CONFIG}
        ${CMAKE_SOURCE_DIR}/tools/render_config.py
        ${CMAKE_SOURCE_DIR}/templates/sdkconfig.defaults_wifi
        ${CMAKE_SOURCE_DIR}/templates/sdkconfig.defaults_thread
        ${CMAKE_SOURCE_DIR}/templates/sdkconfig.defaults_wifi_thread
        ${CMAKE_SOURCE_DIR}/templates/partitions.csv_4MB
        ${CMAKE_SOURCE_DIR}/templates/partitions.csv_8MB
        ${CMAKE_SOURCE_DIR}/templates/partitions.csv_16MB
    COMMENT "Rendering C++ config from ${PARSED_CONFIG}"
    VERBATIM
)

# Add a custom target to depend on the generated file. This ensures it's generated.
add_custom_target(generate_config ALL DEPENDS ${GENERATED_HEADER} ${PARSED_CONFIG})
