add_library(breakerstool_driver SHARED
    src/driver_main.cpp
    src/device_provider.cpp
    src/ipc_server.cpp
    src/input_reader.cpp
    src/pose_hook.cpp
    src/virtual_controller.cpp
)

target_include_directories(breakerstool_driver PRIVATE
    ${OPENVR_SDK_PATH}/headers
    ${CMAKE_SOURCE_DIR}/shared
)

if(WIN32)
    target_link_libraries(breakerstool_driver PRIVATE
        ${OPENVR_SDK_PATH}/lib/win64/openvr_api.lib
    )
else()
    target_link_libraries(breakerstool_driver PRIVATE
        ${OPENVR_SDK_PATH}/lib/linux64/libopenvr_api.so
        pthread dl
    )
    # vtable patching needs -fno-strict-aliasing; visibility hides internal symbols
    target_compile_options(breakerstool_driver PRIVATE
        -fno-strict-aliasing -fvisibility=hidden
    )
endif()

set_target_properties(breakerstool_driver PROPERTIES
    OUTPUT_NAME "driver_ovrdancers"
    PREFIX ""                          # no "lib" prefix on Linux
    # Use per-config properties so MSVC doesn't append Release/ or Debug/ subdirectory
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/driver/bin/win64"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/driver/bin/win64"
    RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/driver/bin/win64"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/driver/bin/linux64" # Linux
)

# Copy manifest alongside the DLL/SO
add_custom_command(TARGET breakerstool_driver POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/driver.vrdrivermanifest"
        "${CMAKE_BINARY_DIR}/driver/driver.vrdrivermanifest"
)

# Auto-deploy to SteamVR drivers folder after each build (Windows only)
if(WIN32)
    # Detect SteamVR path from registry, fall back to default
    get_filename_component(_steamvr_root
        "[HKEY_CURRENT_USER\\Software\\Valve\\Steam;SteamPath]/steamapps/common/SteamVR"
        ABSOLUTE)
    set(STEAMVR_DRIVER_DIR "${_steamvr_root}/drivers/00ovrdancers/bin/win64"
        CACHE PATH "SteamVR driver bin/win64 output directory")
    add_custom_command(TARGET breakerstool_driver POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:breakerstool_driver>"
            "${STEAMVR_DRIVER_DIR}/driver_ovrdancers.dll"
        COMMENT "Deploying driver_ovrdancers.dll to SteamVR"
    )
endif()
