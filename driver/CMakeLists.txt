add_library(breakerstool_driver SHARED
    src/driver_main.cpp
    src/device_provider.cpp
    src/virtual_controller.cpp
    src/ipc_server.cpp
    src/input_reader.cpp
    src/input_hook.cpp
    src/pose_hook.cpp
)

target_include_directories(breakerstool_driver PRIVATE
    ${OPENVR_SDK_PATH}/headers
    ${CMAKE_SOURCE_DIR}/shared
)

target_link_libraries(breakerstool_driver PRIVATE
    ${OPENVR_SDK_PATH}/lib/win64/openvr_api.lib
)

set_target_properties(breakerstool_driver PROPERTIES
    OUTPUT_NAME "driver_ovrdancers"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/driver/bin/win64"
)

# Copy manifest alongside the DLL
add_custom_command(TARGET breakerstool_driver POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/driver.vrdrivermanifest"
        "${CMAKE_BINARY_DIR}/driver/driver.vrdrivermanifest"
)
