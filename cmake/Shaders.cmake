set(SHADER_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(SHADER_SOURCES cube.vert cube.frag grid.vert grid.frag)
set(SHADER_OUTPUTS)

foreach(SHADER IN LISTS SHADER_SOURCES)
  set(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}")
  set(OUTPUT "${SHADER_OUTPUT_DIRECTORY}/${SHADER}.spv")

  add_custom_command(
    OUTPUT "${OUTPUT}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${SHADER_OUTPUT_DIRECTORY}"
    COMMAND Vulkan::glslc --target-env=vulkan1.1 -MD -MF "${OUTPUT}.d"
            "${SOURCE}" -o "${OUTPUT}"
    DEPENDS "${SOURCE}"
    DEPFILE "${OUTPUT}.d"
    VERBATIM
  )
  list(APPEND SHADER_OUTPUTS "${OUTPUT}")
endforeach()

add_custom_target(shaders DEPENDS ${SHADER_OUTPUTS})
