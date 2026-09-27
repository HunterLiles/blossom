#pragma once

#include "math/math.hpp"

namespace camera {

struct Camera {
  math::Vec3 position = {0.0f, 0.0f, -3.0f};
  // Rotates camera space (+X right, +Y up, +Z forward) into world space.
  math::Quat orientation{};
  float fov_y = math::radians(90.0f);
  float near_plane = 0.1f;
  float far_plane = 100.0f;
};

// `yaw` turns right around world +Y, `pitch` tilts up. Radians, no roll.
math::Quat orientation_from_yaw_pitch(float yaw, float pitch);

// Points the camera at `target` without roll.
void look_at(Camera& camera, math::Vec3 target);

math::Vec3 forward(const Camera& camera);
math::Vec3 right(const Camera& camera);
math::Vec3 up(const Camera& camera);

math::Mat4 view_matrix(const Camera& camera);
math::Mat4 projection_matrix(const Camera& camera, float aspect);

} // namespace camera
