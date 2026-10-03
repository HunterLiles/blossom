#version 450

#extension GL_GOOGLE_include_directive : require
#include "objects.glsl"

// Match UniformData's row-major matrices and std140 buffer layout.
layout(set = 0, binding = 0, std140, row_major) uniform Scene {
  mat4 mvp;
  mat4 viewProjection;
  float globalLight;
};

layout(location = 0) out vec3 color;

void main() {
  PrimitiveVertex vertex = cubeVertex(gl_VertexIndex);
  gl_Position = vec4(vertex.position, 1.0) * mvp;
  color = vertex.color * globalLight;
}
