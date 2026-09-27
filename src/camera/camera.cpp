#include "camera/camera.hpp"

#include <algorithm>
#include <cmath>

namespace camera {

static constexpr math::Vec3 WORLD_RIGHT = {1.0f, 0.0f, 0.0f};
static constexpr math::Vec3 WORLD_UP = {0.0f, 1.0f, 0.0f};
static constexpr math::Vec3 WORLD_FORWARD = {0.0f, 0.0f, 1.0f};

math::Quat orientation_from_yaw_pitch(float yaw, float pitch) {
  // Positive rotations turn clockwise looking down the axis (left-handed), so
  // +yaw around +Y turns right and -pitch around +X tilts up.
  math::Quat yaw_rotation = math::from_axis_angle(WORLD_UP, yaw);
  math::Quat pitch_rotation = math::from_axis_angle(WORLD_RIGHT, -pitch);
  return math::normalize(yaw_rotation * pitch_rotation);
}

void look_at(Camera& camera, math::Vec3 target) {
  math::Vec3 direction = math::normalize(target - camera.position);
  float yaw = std::atan2(direction.x, direction.z);
  float pitch = std::asin(std::clamp(direction.y, -1.0f, 1.0f));
  camera.orientation = orientation_from_yaw_pitch(yaw, pitch);
}

math::Vec3 forward(const Camera& camera) {
  return math::rotate(camera.orientation, WORLD_FORWARD);
}

math::Vec3 right(const Camera& camera) {
  return math::rotate(camera.orientation, WORLD_RIGHT);
}

math::Vec3 up(const Camera& camera) { return math::rotate(camera.orientation, WORLD_UP); }

math::Mat4 view_matrix(const Camera& camera) {
  // Inverse of the camera's world transform: undo the translation, then the
  // rotation.
  return math::to_mat4(math::conjugate(camera.orientation)) *
         math::translation(-camera.position);
}

math::Mat4 projection_matrix(const Camera& camera, float aspect) {
  return math::perspective(camera.fov_y, aspect, camera.near_plane, camera.far_plane);
}

} // namespace camera
