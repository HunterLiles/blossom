#pragma once

#include <cmath>
#include <numbers>

namespace math {
struct Vec3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

struct Quaternion {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float w = 1.0f;
};

struct Mat4 {
  // Row-major storage. Points are row vectors: point * model * view * projection.
  float value[16]{};
};

inline float radians(float degrees) {
  return degrees * (std::numbers::pi_v<float> / 180.0f);
}

inline Vec3 add(Vec3 a, Vec3 b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

inline Vec3 subtract(Vec3 a, Vec3 b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline Vec3 scale(Vec3 v, float s) {
  return {v.x * s, v.y * s, v.z * s};
}

inline float dot(Vec3 a, Vec3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 cross(Vec3 a, Vec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline Vec3 normalize(Vec3 v) {
  const float length = std::sqrt(dot(v, v));
  return length > 0.0f ? scale(v, 1.0f / length) : Vec3{};
}

inline Quaternion normalize(Quaternion q) {
  const float length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  return length > 0.0f
             ? Quaternion{q.x / length, q.y / length, q.z / length, q.w / length}
             : Quaternion{};
}

inline Quaternion multiply(Quaternion a, Quaternion b) {
  return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
          a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
          a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
          a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

inline Quaternion axisAngle(Vec3 axis, float radians) {
  const float half = radians * 0.5f;
  const float sine = std::sin(half);
  axis = normalize(axis);
  return {axis.x * sine, axis.y * sine, axis.z * sine, std::cos(half)};
}

inline Vec3 rotate(Quaternion q, Vec3 v) {
  const Quaternion vector{v.x, v.y, v.z, 0.0f};
  const Quaternion conjugate{-q.x, -q.y, -q.z, q.w};
  const Quaternion result = multiply(multiply(q, vector), conjugate);
  return {result.x, result.y, result.z};
}

inline Mat4 identity() {
  Mat4 result{};
  result.value[0] = 1.0f;
  result.value[5] = 1.0f;
  result.value[10] = 1.0f;
  result.value[15] = 1.0f;
  return result;
}

inline Mat4 multiply(const Mat4 &a, const Mat4 &b) {
  Mat4 result{};
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) {
      for (int i = 0; i < 4; ++i) {
        result.value[row * 4 + column] += a.value[row * 4 + i] * b.value[i * 4 + column];
      }
    }
  }
  return result;
}

inline Mat4 translation(Vec3 position) {
  Mat4 result = identity();
  result.value[12] = position.x;
  result.value[13] = position.y;
  result.value[14] = position.z;
  return result;
}

inline Mat4 rotation(Quaternion q) {
  q = normalize(q);
  const float xx = q.x * q.x;
  const float yy = q.y * q.y;
  const float zz = q.z * q.z;
  const float xy = q.x * q.y;
  const float xz = q.x * q.z;
  const float yz = q.y * q.z;
  const float xw = q.x * q.w;
  const float yw = q.y * q.w;
  const float zw = q.z * q.w;

  // clang-format off
  return {{1.0f - 2.0f * (yy + zz), 2.0f * (xy + zw), 2.0f * (xz - yw), 0.0f,
           2.0f * (xy - zw), 1.0f - 2.0f * (xx + zz), 2.0f * (yz + xw), 0.0f,
           2.0f * (xz + yw), 2.0f * (yz - xw), 1.0f - 2.0f * (xx + yy), 0.0f,
           0.0f, 0.0f, 0.0f, 1.0f}};
  // clang-format on
}

inline Mat4 lookAt(Vec3 eye, Vec3 forward, Vec3 up) {
  const Vec3 z = scale(normalize(forward), -1.0f);
  const Vec3 x = normalize(cross(up, z));
  const Vec3 y = cross(z, x);

  // clang-format off
  return {{x.x, y.x, z.x, 0.0f,
           x.y, y.y, z.y, 0.0f,
           x.z, y.z, z.z, 0.0f,
           -dot(x, eye), -dot(y, eye), -dot(z, eye), 1.0f}};
  // clang-format on
}

inline Mat4
perspective(float verticalFov, float aspect, float nearPlane, float farPlane) {
  const float focal = 1.0f / std::tan(verticalFov * 0.5f);
  Mat4 result{};
  result.value[0] = focal / aspect;
  result.value[5] = focal;
  result.value[10] = farPlane / (nearPlane - farPlane);
  result.value[11] = -1.0f;
  result.value[14] = (nearPlane * farPlane) / (nearPlane - farPlane);
  return result;
}
} // namespace math
