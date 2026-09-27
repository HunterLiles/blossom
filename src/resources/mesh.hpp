#pragma once

#include "math/math.hpp"

#include <cstdint>
#include <vector>

namespace resources {

struct Vertex {
  math::Vec3 position;
  math::Vec3 normal;
  math::Vec3 color;
  // Texture coordinates: (0, 0) is the top-left of the image, +V goes down.
  math::Vec2 uv;
};

struct MeshData {
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
};

// Unit-sized primitives centered on the origin. For every triangle (a, b, c),
// cross(b - a, c - a) points outward, so front faces are clockwise when seen
// from outside in the left-handed convention. UVs keep textures upright and
// unmirrored when seen from outside.
MeshData make_cube(math::Vec3 color);
MeshData make_pyramid(math::Vec3 color);
MeshData make_sphere(math::Vec3 color, uint32_t segments, uint32_t rings);

} // namespace resources
