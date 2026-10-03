#version 450

layout(set = 0, binding = 0, std140, row_major) uniform Scene {
  mat4 mvp;
  mat4 viewProjection;
  float globalLight;
};

layout(location = 0) out vec3 color;

const int linesPerDirection = 21;
const int gridLineCount = linesPerDirection * 2;
const float gridExtent = 5.0;
const float gridSpacing = 0.5;
const float axisLength = 2.0;

void main() {
  int gridLine = gl_VertexIndex / 2;
  bool endpoint = (gl_VertexIndex % 2) != 0;
  vec3 position;
  color = vec3(0.28, 0.33, 0.40);

  if (gridLine < linesPerDirection) {
    float offset = -gridExtent + gridLine * gridSpacing;
    position = vec3(offset, 0.0, endpoint ? gridExtent : -gridExtent);
  } else if (gridLine < gridLineCount) {
    float offset = -gridExtent + (gridLine - linesPerDirection) * gridSpacing;
    position = vec3(endpoint ? gridExtent : -gridExtent, 0.0, offset);
  } else if (gridLine == gridLineCount) {
    position = vec3(endpoint ? axisLength : 0.0, 0.0, 0.0);
    color = vec3(1.0, 0.2, 0.2); // X
  } else if (gridLine == gridLineCount + 1) {
    position = vec3(0.0, endpoint ? axisLength : 0.0, 0.0);
    color = vec3(0.2, 1.0, 0.2); // Y
  } else {
    position = vec3(0.0, 0.0, endpoint ? axisLength : 0.0);
    color = vec3(0.2, 0.45, 1.0); // Z
  }
  // The grid belongs to the world, not to the cube's model transform.
  gl_Position = vec4(position, 1.0) * viewProjection;
}
