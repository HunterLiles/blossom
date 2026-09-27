#pragma once

#include "camera/camera.hpp"

namespace platform {
struct Window;
}

namespace input {
struct InputState;
}

namespace camera {

// Fly camera. Starts when the right mouse button is pressed over the view
// and stays active while it is held:
//   mouse           look (yaw and pitch)
//   W / S           forward / back along the view direction
//   A / D           left / right
//   Space / Ctrl    up / down along world +Y
//   Shift           sprint
struct CameraController {
  // Radians. Yaw turns right, pitch tilts up.
  float yaw = 0.0f;
  float pitch = 0.0f;
  float move_speed = 3.0f;          // units per second
  float sprint_multiplier = 1.5f;   // applied to move_speed while Shift is held
  float look_sensitivity = 0.0025f; // radians per pixel
  bool isActive = false;
};

// Starts the controller from the camera's current view direction.
CameraController make_controller(const Camera& camera);

// `isViewHovered`: whether the cursor is over the camera's view (e.g. the
// GUI's Scene window). Only checked when the right button goes down.
void update_controller(CameraController& controller, Camera& camera,
                       input::InputState& input, const platform::Window& window,
                       bool isViewHovered, float delta_seconds);

} // namespace camera
