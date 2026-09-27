#pragma once

#include "math/vec.hpp"

#include <cmath>

namespace math {

// Column-major: columns[c] is column c, laid out contiguously. This matches
// HLSL's default packing, so shaders use mul(matrix, vector).
// Conventions: left-handed, +X right, +Y up, +Z forward (into the screen).
// Clip space keeps +Y up with depth 0..1; renderers that need Y down (Vulkan)
// flip it in their viewport.
struct Mat4 {
  Vec4 columns[4]{};
};

static_assert(sizeof(Mat4) == 16 * sizeof(float));

constexpr Mat4 identity() {
  return {{
      {1.0f, 0.0f, 0.0f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {0.0f, 0.0f, 1.0f, 0.0f},
      {0.0f, 0.0f, 0.0f, 1.0f},
  }};
}

constexpr Vec4 operator*(const Mat4& m, Vec4 v) {
  return m.columns[0] * v.x + m.columns[1] * v.y + m.columns[2] * v.z +
         m.columns[3] * v.w;
}

constexpr Mat4 operator*(const Mat4& a, const Mat4& b) {
  return {{
      a * b.columns[0],
      a * b.columns[1],
      a * b.columns[2],
      a * b.columns[3],
  }};
}

constexpr Mat4 translation(Vec3 offset) {
  Mat4 m = identity();
  m.columns[3] = {offset.x, offset.y, offset.z, 1.0f};
  return m;
}

constexpr Mat4 scale(Vec3 factors) {
  Mat4 m = identity();
  m.columns[0].x = factors.x;
  m.columns[1].y = factors.y;
  m.columns[2].z = factors.z;
  return m;
}

// Rotation of `angle` radians around `axis`. Positive angles turn clockwise
// when looking from the tip of `axis` toward the origin (left-handed).
inline Mat4 rotation(Vec3 axis, float angle) {
  Vec3 a = normalize(axis);
  float c = std::cos(angle);
  float s = std::sin(angle);
  float t = 1.0f - c;

  return {{
      {t * a.x * a.x + c, t * a.x * a.y + s * a.z, t * a.x * a.z - s * a.y, 0.0f},
      {t * a.x * a.y - s * a.z, t * a.y * a.y + c, t * a.y * a.z + s * a.x, 0.0f},
      {t * a.x * a.z + s * a.y, t * a.y * a.z - s * a.x, t * a.z * a.z + c, 0.0f},
      {0.0f, 0.0f, 0.0f, 1.0f},
  }};
}

// `fov_y` in radians. Maps view depth near_plane..far_plane to 0..1.
inline Mat4 perspective(float fov_y, float aspect, float near_plane, float far_plane) {
  float f = 1.0f / std::tan(fov_y * 0.5f);
  float depth = far_plane / (far_plane - near_plane);

  return {{
      {f / aspect, 0.0f, 0.0f, 0.0f},
      {0.0f, f, 0.0f, 0.0f},
      {0.0f, 0.0f, depth, 1.0f},
      {0.0f, 0.0f, -near_plane * depth, 0.0f},
  }};
}

inline Mat4 look_at(Vec3 eye, Vec3 target, Vec3 up) {
  Vec3 forward = normalize(target - eye);
  Vec3 right = normalize(cross(up, forward));
  Vec3 camera_up = cross(forward, right);

  return {{
      {right.x, camera_up.x, forward.x, 0.0f},
      {right.y, camera_up.y, forward.y, 0.0f},
      {right.z, camera_up.z, forward.z, 0.0f},
      {-dot(right, eye), -dot(camera_up, eye), -dot(forward, eye), 1.0f},
  }};
}

} // namespace math
