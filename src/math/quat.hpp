#pragma once

#include "math/mat.hpp"
#include "math/vec.hpp"

#include <cmath>

namespace math {

// Unit quaternion rotation. Default-constructed is the identity.
// a * b applies b first, then a (same order as matrices).
struct Quat {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float w = 1.0f;
};

// Rotation of `angle` radians around `axis`, matching math::rotation.
inline Quat from_axis_angle(Vec3 axis, float angle) {
  Vec3 a = normalize(axis);
  float s = std::sin(angle * 0.5f);
  return {a.x * s, a.y * s, a.z * s, std::cos(angle * 0.5f)};
}

constexpr Quat operator*(Quat a, Quat b) {
  return {
      a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
      a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
      a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
      a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
  };
}

// The inverse for unit quaternions.
constexpr Quat conjugate(Quat q) { return {-q.x, -q.y, -q.z, q.w}; }

constexpr float dot(Quat a, Quat b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

inline Quat normalize(Quat q) {
  float inverse_length = 1.0f / std::sqrt(dot(q, q));
  return {q.x * inverse_length, q.y * inverse_length, q.z * inverse_length,
          q.w * inverse_length};
}

constexpr Vec3 rotate(Quat q, Vec3 v) {
  Vec3 u = {q.x, q.y, q.z};
  Vec3 t = 2.0f * cross(u, v);
  return v + q.w * t + cross(u, t);
}

constexpr Mat4 to_mat4(Quat q) {
  float xx = q.x * q.x;
  float yy = q.y * q.y;
  float zz = q.z * q.z;
  float xy = q.x * q.y;
  float xz = q.x * q.z;
  float yz = q.y * q.z;
  float wx = q.w * q.x;
  float wy = q.w * q.y;
  float wz = q.w * q.z;

  return {{
      {1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz), 2.0f * (xz - wy), 0.0f},
      {2.0f * (xy - wz), 1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx), 0.0f},
      {2.0f * (xz + wy), 2.0f * (yz - wx), 1.0f - 2.0f * (xx + yy), 0.0f},
      {0.0f, 0.0f, 0.0f, 1.0f},
  }};
}

} // namespace math
