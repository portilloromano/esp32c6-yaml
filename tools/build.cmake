
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

# Generate C++ header from YAML config
set(CONFIG_YAML ${CMAKE_SOURCE_DIR}/config.yaml)
set(GENERATED_HEADER ${CMAKE_BINARY_DIR}/generated_config.h)

add_custom_command(
    OUTPUT ${GENERATED_HEADER}
    COMMAND ${PYTHON} ${CMAKE_SOURCE_DIR}/tools/config_generator.py ${CONFIG_YAML} ${GENERATED_HEADER}
    DEPENDS ${CONFIG_YAML} ${CMAKE_SOURCE_DIR}/tools/config_generator.py pip_requirements
    COMMENT "Generating C++ config from ${CONFIG_YAML}"
    VERBATIM
)

# Add a custom target to depend on the generated file. This ensures it's generated.
add_custom_target(generate_config ALL DEPENDS ${GENERATED_HEADER})


