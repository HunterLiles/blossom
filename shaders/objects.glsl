#ifndef BLOSSOM_OBJECTS_GLSL
#define BLOSSOM_OBJECTS_GLSL

struct PrimitiveVertex {
  vec3 position;
  vec3 color;
};

// Unit cube centered at the origin. Draw 36 vertices as a triangle list.
PrimitiveVertex cubeVertex(int vertexIndex) {
  const vec3 corners[8] = vec3[](
      vec3(-0.5, -0.5, -0.5),
      vec3( 0.5, -0.5, -0.5),
      vec3( 0.5,  0.5, -0.5),
      vec3(-0.5,  0.5, -0.5),
      vec3(-0.5, -0.5,  0.5),
      vec3( 0.5, -0.5,  0.5),
      vec3( 0.5,  0.5,  0.5),
      vec3(-0.5,  0.5,  0.5));

  const int indices[36] = int[](
      4, 5, 6, 4, 6, 7, // +Z
      1, 0, 3, 1, 3, 2, // -Z
      0, 4, 7, 0, 7, 3, // -X
      5, 1, 2, 5, 2, 6, // +X
      3, 7, 6, 3, 6, 2, // +Y
      0, 1, 5, 0, 5, 4  // -Y
  );

  const vec3 faceColors[6] = vec3[](
      vec3(1.0, 0.25, 0.35),
      vec3(0.25, 0.7, 1.0),
      vec3(0.25, 1.0, 0.5),
      vec3(1.0, 0.8, 0.25),
      vec3(0.7, 0.35, 1.0),
      vec3(0.3, 1.0, 0.9));

  return PrimitiveVertex(corners[indices[vertexIndex]], faceColors[vertexIndex / 6]);
}

#endif
