#include "camera/camera_controller.hpp"

#include "input/input.hpp"

#include <algorithm>
#include <cmath>

namespace camera {

// Just short of straight up/down so yaw stays well defined.
static constexpr float MAX_PITCH = math::radians(89.0f);
static constexpr math::Vec3 WORLD_UP = {0.0f, 1.0f, 0.0f};

static float axis(const input::InputState& input, input::Key positive,
                  input::Key negative) {
  return (input::is_key_down(input, positive) ? 1.0f : 0.0f) -
         (input::is_key_down(input, negative) ? 1.0f : 0.0f);
}

CameraController make_controller(const Camera& camera) {
  math::Vec3 direction = forward(camera);

  CameraController controller{};
  controller.yaw = std::atan2(direction.x, direction.z);
  controller.pitch = std::asin(std::clamp(direction.y, -1.0f, 1.0f));
  return controller;
}

void update_controller(CameraController& controller, Camera& camera,
                       input::InputState& input, const platform::Window& window,
                       bool isViewHovered, float delta_seconds) {
  bool isRightDown = input::is_mouse_button_down(input, input::MouseButton::Right);
  if (!controller.isActive && isRightDown && isViewHovered) {
    input::set_cursor_captured(input, window, true);
    controller.isActive = true;
  } else if (controller.isActive && !isRightDown) {
    input::set_cursor_captured(input, window, false);
    controller.isActive = false;
  }
  if (!controller.isActive) {
    return;
  }

  // Mouse +Y is down, so moving the mouse down tilts the view down.
  controller.yaw += input.mouse_dx * controller.look_sensitivity;
  controller.pitch -= input.mouse_dy * controller.look_sensitivity;
  controller.yaw = std::remainder(controller.yaw, 2.0f * math::PI);
  controller.pitch = std::clamp(controller.pitch, -MAX_PITCH, MAX_PITCH);
  camera.orientation = orientation_from_yaw_pitch(controller.yaw, controller.pitch);

  math::Vec3 direction = right(camera) * axis(input, input::Key::D, input::Key::A) +
                         WORLD_UP * axis(input, input::Key::Space, input::Key::LeftControl) +
                         forward(camera) * axis(input, input::Key::W, input::Key::S);

  // Normalize so diagonals (e.g. W + D) move at the same speed as one key.
  float length = math::length(direction);
  if (length > 0.0f) {
    float speed = controller.move_speed;
    if (input::is_key_down(input, input::Key::LeftShift)) {
      speed *= controller.sprint_multiplier;
    }
    camera.position = camera.position + direction * (speed * delta_seconds / length);
  }
}

} // namespace camera
