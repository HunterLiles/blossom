cmake_minimum_required(VERSION 3.25)
project(blossom LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE
      Debug
      CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
find_package(Vulkan 1.4 REQUIRED COMPONENTS dxc)

set(GLFW_BUILD_EXAMPLES
    OFF
    CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS
    OFF
    CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS
    OFF
    CACHE BOOL "" FORCE)
set(GLFW_INSTALL
    OFF
    CACHE BOOL "" FORCE)
add_subdirectory(external/glfw)
add_subdirectory(external/imgui)
# Treat ImGui's headers as system headers so the engine's strict warnings
# don't fire on its internals.
set_target_properties(imgui PROPERTIES SYSTEM TRUE)

# ---------------------------------------------------------------------------
# Engine
# ---------------------------------------------------------------------------
file(GLOB_RECURSE BLOSSOM_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp)

add_executable(blossom ${BLOSSOM_SOURCES})

target_include_directories(blossom PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# Header-only libraries. SYSTEM keeps their warnings out of the engine's strict flags.
target_include_directories(blossom SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/external/stb)
target_link_libraries(blossom PRIVATE glfw imgui Vulkan::Vulkan)

target_compile_definitions(
  blossom
  PRIVATE GLFW_INCLUDE_NONE
          BLOSSOM_SHADER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
          BLOSSOM_SHADER_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders"
          BLOSSOM_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
          BLOSSOM_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/config")

if(MSVC)
  target_compile_options(blossom PRIVATE /W4 /permissive- /utf-8)
  target_compile_definitions(blossom PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
  target_compile_options(blossom PRIVATE -Wall -Wextra -Wpedantic -Wshadow
                                         -Wconversion)
endif()

# ---------------------------------------------------------------------------
# Shaders (HLSL -> SPIR-V via DXC) Stage comes from the file name: *.vs.hlsl,
# *.ps.hlsl, *.cs.hlsl. Entry point is main. Empty shader files are skipped.
# ---------------------------------------------------------------------------
set(BLOSSOM_SHADER_MODEL 6_6)
set(BLOSSOM_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(BLOSSOM_SHADER_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)

file(GLOB_RECURSE BLOSSOM_SHADERS CONFIGURE_DEPENDS
     ${BLOSSOM_SHADER_SOURCE_DIR}/*.hlsl)
file(GLOB_RECURSE BLOSSOM_SHADER_INCLUDES CONFIGURE_DEPENDS
     ${BLOSSOM_SHADER_SOURCE_DIR}/*.hlsli)

set(BLOSSOM_SPIRV_OUTPUTS)
foreach(shader ${BLOSSOM_SHADERS})
  # Reconfigure when an empty shader is edited so it gets picked up.
  file(SIZE ${shader} shader_size)
  if(shader_size EQUAL 0)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${shader})
    continue()
  endif()

  get_filename_component(shader_name ${shader} NAME)
  if(shader_name MATCHES "\\.vs\\.hlsl$")
    set(stage vs)
  elseif(shader_name MATCHES "\\.ps\\.hlsl$")
    set(stage ps)
  elseif(shader_name MATCHES "\\.cs\\.hlsl$")
    set(stage cs)
  else()
    message(FATAL_ERROR "Shader has no stage suffix (.vs/.ps/.cs): ${shader}")
  endif()

  file(RELATIVE_PATH relative ${BLOSSOM_SHADER_SOURCE_DIR} ${shader})
  string(REGEX REPLACE "\\.hlsl$" ".spv" relative ${relative})
  set(output ${BLOSSOM_SHADER_BINARY_DIR}/${relative})
  get_filename_component(output_dir ${output} DIRECTORY)

  add_custom_command(
    OUTPUT ${output}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${output_dir}
    COMMAND
      Vulkan::dxc_exe -spirv -fspv-target-env=vulkan1.3 -T
      ${stage}_${BLOSSOM_SHADER_MODEL} -E main -HV 2021 -I
      ${BLOSSOM_SHADER_SOURCE_DIR} $<$<CONFIG:Debug>:-Zi> $<$<CONFIG:Debug>:-Od>
      -Fo ${output} ${shader}
    DEPENDS ${shader} ${BLOSSOM_SHADER_INCLUDES}
    COMMENT "Compiling shader ${relative}"
    VERBATIM)
  list(APPEND BLOSSOM_SPIRV_OUTPUTS ${output})
endforeach()

add_custom_target(
  blossom_shaders
  DEPENDS ${BLOSSOM_SPIRV_OUTPUTS}
  SOURCES ${BLOSSOM_SHADERS} ${BLOSSOM_SHADER_INCLUDES})
add_dependencies(blossom blossom_shaders)
