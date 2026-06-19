cmake_minimum_required(VERSION 3.20)
project(BreakersTool CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Vendor paths ──────────────────────────────────────────────────────────────
# Set these to wherever you clone the SDKs, or use vcpkg / FetchContent
set(OPENVR_SDK_PATH   "${CMAKE_SOURCE_DIR}/vendor/openvr"  CACHE PATH "OpenVR SDK root")
set(IMGUI_PATH        "${CMAKE_SOURCE_DIR}/vendor/imgui"   CACHE PATH "Dear ImGui root")
set(GLFW_PATH         "${CMAKE_SOURCE_DIR}/vendor/glfw"    CACHE PATH "GLFW root")
set(STB_PATH          "${CMAKE_SOURCE_DIR}/vendor/stb"     CACHE PATH "stb headers root")

add_subdirectory(driver)
add_subdirectory(overlay)
