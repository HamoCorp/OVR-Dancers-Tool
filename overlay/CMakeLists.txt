# ── ImGui sources ─────────────────────────────────────────────────────────────
set(IMGUI_SOURCES
    ${IMGUI_PATH}/imgui.cpp
    ${IMGUI_PATH}/imgui_draw.cpp
    ${IMGUI_PATH}/imgui_tables.cpp
    ${IMGUI_PATH}/imgui_widgets.cpp
    ${IMGUI_PATH}/backends/imgui_impl_glfw.cpp
    ${IMGUI_PATH}/backends/imgui_impl_opengl3.cpp
)

add_executable(OVRDancersTool
    src/main.cpp
    src/ipc_client.cpp
    src/persistence.cpp
    src/ui/main_window.cpp
    ${IMGUI_SOURCES}
)

target_include_directories(OVRDancersTool PRIVATE
    ${IMGUI_PATH}
    ${IMGUI_PATH}/backends
    ${GLFW_PATH}/include
    ${OPENVR_SDK_PATH}/headers
    ${STB_PATH}
    ${CMAKE_SOURCE_DIR}/shared
)

# GLFW — prefer pre-built, or build from source
find_library(GLFW_LIB glfw3
    PATHS ${GLFW_PATH}/lib-vc2022
          ${GLFW_PATH}/lib
    NO_DEFAULT_PATH
)
if(NOT GLFW_LIB)
    message(FATAL_ERROR "GLFW library not found. Set GLFW_PATH or build GLFW first.")
endif()

target_link_libraries(OVRDancersTool PRIVATE
    ${GLFW_LIB}
    ${OPENVR_SDK_PATH}/lib/win64/openvr_api.lib
    opengl32
)

set_target_properties(OVRDancersTool PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/overlay"
)
target_link_options(OVRDancersTool PRIVATE /SUBSYSTEM:CONSOLE)

# Copy resources next to the executable
add_custom_command(TARGET OVRDancersTool POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/resources"
        "$<TARGET_FILE_DIR:OVRDancersTool>/resources"
)

# Copy openvr_api.dll
add_custom_command(TARGET OVRDancersTool POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${OPENVR_SDK_PATH}/bin/win64/openvr_api.dll"
        "$<TARGET_FILE_DIR:OVRDancersTool>/openvr_api.dll"
)
