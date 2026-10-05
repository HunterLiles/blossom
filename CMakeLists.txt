cmake_minimum_required(VERSION 3.24)
project(
  blossom
  VERSION 0.1.0
  LANGUAGES C CXX)

include(FetchContent)
find_package(Vulkan REQUIRED COMPONENTS glslc)

include(cmake/GLFW.cmake)
include(cmake/ImGui.cmake)
include(cmake/Shaders.cmake)

add_executable(blossom src/main.cpp src/Input.cpp src/Interface.cpp
                       src/Renderer.cpp src/WorkspaceLayout.cpp)
target_compile_features(blossom PRIVATE cxx_std_20)
set_target_properties(blossom PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(blossom PRIVATE include)
target_link_libraries(blossom PRIVATE Vulkan::Vulkan glfw imgui)
target_compile_definitions(
  blossom
  PRIVATE GLFW_INCLUDE_NONE GLFW_INCLUDE_VULKAN
          "SHADER_DIRECTORY=\"${SHADER_OUTPUT_DIRECTORY}\""
          "LAYOUT_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/layouts\"")
add_dependencies(blossom shaders)

if(WIN32)
  target_compile_definitions(blossom PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
