# Dear ImGui (docking) with the GLFW and Vulkan backends.
# Usage from the main CMakeLists.txt:
#   add_subdirectory(external/glfw)
#   add_subdirectory(external/imgui)
#   target_link_libraries(<target> PRIVATE imgui)

cmake_minimum_required(VERSION 3.20)
project(imgui LANGUAGES CXX)

if(NOT TARGET glfw)
    message(FATAL_ERROR "imgui: add_subdirectory(external/glfw) must come before external/imgui")
endif()

find_package(Vulkan REQUIRED)

add_library(imgui STATIC
    imgui.cpp
    imgui_demo.cpp
    imgui_draw.cpp
    imgui_tables.cpp
    imgui_widgets.cpp
    backends/imgui_impl_glfw.cpp
    backends/imgui_impl_vulkan.cpp
)
add_library(imgui::imgui ALIAS imgui)

target_include_directories(imgui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/backends
)

target_compile_features(imgui PUBLIC cxx_std_11)
target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)
