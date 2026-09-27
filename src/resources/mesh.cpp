#include "resources/mesh.hpp"

#include <cmath>

namespace resources {

using math::Vec2;
using math::Vec3;

// `u` and `v` are half extents with cross(u, v) pointing along `normal`. Seen
// from outside (left-handed), `u` points up the screen and `v` to the right,
// so the texture's V runs along -u and its U along +v.
static void add_quad(MeshData& mesh, Vec3 center, Vec3 u, Vec3 v, Vec3 normal,
                     Vec3 color) {
  uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
  mesh.vertices.push_back({center - u - v, normal, color, Vec2{0.0f, 1.0f}});
  mesh.vertices.push_back({center + u - v, normal, color, Vec2{0.0f, 0.0f}});
  mesh.vertices.push_back({center + u + v, normal, color, Vec2{1.0f, 0.0f}});
  mesh.vertices.push_back({center - u + v, normal, color, Vec2{1.0f, 1.0f}});
  mesh.indices.insert(mesh.indices.end(),
                      {base, base + 1, base + 2, base, base + 2, base + 3});
}

static void add_triangle(MeshData& mesh, Vec3 a, Vec3 b, Vec3 c, Vec2 uv_a, Vec2 uv_b,
                         Vec2 uv_c, Vec3 color) {
  Vec3 normal = math::normalize(math::cross(b - a, c - a));
  uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
  mesh.vertices.push_back({a, normal, color, uv_a});
  mesh.vertices.push_back({b, normal, color, uv_b});
  mesh.vertices.push_back({c, normal, color, uv_c});
  mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2});
}

MeshData make_cube(Vec3 color) {
  constexpr Vec3 X = {1.0f, 0.0f, 0.0f};
  constexpr Vec3 Y = {0.0f, 1.0f, 0.0f};
  constexpr Vec3 Z = {0.0f, 0.0f, 1.0f};

  struct Face {
    Vec3 normal;
    Vec3 u;
    Vec3 v;
  };
  // Side faces use u = +Y so their textures stand upright.
  constexpr Face FACES[] = {
      {X, Y, Z}, {-X, Y, -Z}, {Z, Y, -X}, {-Z, Y, X}, {Y, Z, X}, {-Y, -Z, X},
  };

  MeshData mesh;
  for (const Face& face : FACES) {
    add_quad(mesh, face.normal * 0.5f, face.u * 0.5f, face.v * 0.5f, face.normal, color);
  }
  return mesh;
}

MeshData make_pyramid(Vec3 color) {
  constexpr Vec3 APEX = {0.0f, 0.5f, 0.0f};
  constexpr Vec3 BASE[] = {
      {-0.5f, -0.5f, 0.5f},
      {0.5f, -0.5f, 0.5f},
      {0.5f, -0.5f, -0.5f},
      {-0.5f, -0.5f, -0.5f},
  };

  MeshData mesh;
  add_quad(mesh, {0.0f, -0.5f, 0.0f}, {0.5f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.5f},
           {0.0f, -1.0f, 0.0f}, color);
  // Seen from outside, BASE[i] is the bottom-right corner of each side and
  // BASE[i + 1] the bottom-left.
  for (uint32_t i = 0; i < 4; ++i) {
    add_triangle(mesh, BASE[i], BASE[(i + 1) % 4], APEX, {1.0f, 1.0f}, {0.0f, 1.0f},
                 {0.5f, 0.0f}, color);
  }
  return mesh;
}

MeshData make_sphere(Vec3 color, uint32_t segments, uint32_t rings) {
  MeshData mesh;

  for (uint32_t ring = 0; ring <= rings; ++ring) {
    float theta = math::PI * static_cast<float>(ring) / static_cast<float>(rings);
    for (uint32_t segment = 0; segment <= segments; ++segment) {
      float phi =
          2.0f * math::PI * static_cast<float>(segment) / static_cast<float>(segments);
      Vec3 normal = {
          std::sin(theta) * std::cos(phi),
          std::cos(theta),
          std::sin(theta) * std::sin(phi),
      };
      Vec2 uv = {static_cast<float>(segment) / static_cast<float>(segments),
                 static_cast<float>(ring) / static_cast<float>(rings)};
      mesh.vertices.push_back({normal * 0.5f, normal, color, uv});
    }
  }

  uint32_t stride = segments + 1;
  for (uint32_t ring = 0; ring < rings; ++ring) {
    for (uint32_t segment = 0; segment < segments; ++segment) {
      uint32_t top = ring * stride + segment;
      uint32_t bottom = top + stride;
      mesh.indices.insert(mesh.indices.end(),
                          {top, top + 1, bottom, top + 1, bottom + 1, bottom});
    }
  }
  return mesh;
}

} // namespace resources
