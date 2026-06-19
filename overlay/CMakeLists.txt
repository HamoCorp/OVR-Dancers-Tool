# ── ImGui sources ─────────────────────────────────────────────────────────────
set(IMGUI_SOURCES
    ${IMGUI_PATH}/imgui.cpp
    ${IMGUI_PATH}/imgui_draw.cpp
    ${IMGUI_PATH}/imgui_tables.cpp
    ${IMGUI_PATH}/imgui_widgets.cpp
    ${IMGUI_PATH}/backends/imgui_impl_glfw.cpp
    ${IMGUI_PATH}/backends/imgui_impl_opengl3.cpp
)

if(WIN32)
    set(RC_FILE src/app.rc)
else()
    set(RC_FILE "")
endif()

add_executable(OVRDancersTool
    src/main.cpp
    src/ipc_client.cpp
    src/persistence.cpp
    src/osc_sender.cpp
    src/websocket_server.cpp
    src/ui/main_window.cpp
    ${IMGUI_SOURCES}
    ${RC_FILE}
)

target_include_directories(OVRDancersTool PRIVATE
    ${IMGUI_PATH}
    ${IMGUI_PATH}/backends
    ${GLFW_PATH}/include
    ${OPENVR_SDK_PATH}/headers
    ${STB_PATH}
    ${CMAKE_SOURCE_DIR}/shared
)

# ── GLFW ──────────────────────────────────────────────────────────────────────
if(WIN32)
    find_library(GLFW_LIB glfw3
        PATHS ${GLFW_PATH}/lib-vc2022
              ${GLFW_PATH}/lib
        NO_DEFAULT_PATH
    )
    if(NOT GLFW_LIB)
        message(FATAL_ERROR "GLFW library not found. Set GLFW_PATH or build GLFW first.")
    endif()
else()
    # On Linux prefer a system-installed GLFW, fall back to vendored build
    find_library(GLFW_LIB glfw
        PATHS ${GLFW_PATH}/lib
              ${GLFW_PATH}/lib/linux64
        /usr/lib /usr/local/lib
    )
    if(NOT GLFW_LIB)
        message(FATAL_ERROR "GLFW library not found. Install libglfw3-dev or set GLFW_PATH.")
    endif()
endif()

# ── Compiler options ──────────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(OVRDancersTool PRIVATE /utf-8)
endif()

# ── Link ──────────────────────────────────────────────────────────────────────
if(WIN32)
    target_link_libraries(OVRDancersTool PRIVATE
        ${GLFW_LIB}
        ${OPENVR_SDK_PATH}/lib/win64/openvr_api.lib
        opengl32
    )
    target_link_options(OVRDancersTool PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
else()
    target_link_libraries(OVRDancersTool PRIVATE
        ${GLFW_LIB}
        ${OPENVR_SDK_PATH}/lib/linux64/libopenvr_api.so
        GL dl pthread
    )
    # Also need X11 or Wayland backend for GLFW on Linux
    find_package(X11)
    if(X11_FOUND)
        target_link_libraries(OVRDancersTool PRIVATE ${X11_LIBRARIES})
    endif()
endif()

# ── Output ────────────────────────────────────────────────────────────────────
set_target_properties(OVRDancersTool PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/overlay/Release"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/overlay/Debug"
    RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/overlay/Release"
)

# ── Resources ─────────────────────────────────────────────────────────────────
add_custom_command(TARGET OVRDancersTool POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/resources"
        "$<TARGET_FILE_DIR:OVRDancersTool>/resources"
)

if(WIN32)
    add_custom_command(TARGET OVRDancersTool POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${OPENVR_SDK_PATH}/bin/win64/openvr_api.dll"
            "$<TARGET_FILE_DIR:OVRDancersTool>/openvr_api.dll"
    )

else()
    add_custom_command(TARGET OVRDancersTool POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${OPENVR_SDK_PATH}/bin/linux64/libopenvr_api.so"
            "$<TARGET_FILE_DIR:OVRDancersTool>/libopenvr_api.so"
    )
endif()
