#include "input/input.hpp"

#include "platform/window.hpp"

#include <GLFW/glfw3.h>

namespace input {

static constexpr int KEY_CODES[KEY_COUNT] = {
    GLFW_KEY_W,     GLFW_KEY_A,            GLFW_KEY_S,          GLFW_KEY_D,
    GLFW_KEY_SPACE, GLFW_KEY_LEFT_CONTROL, GLFW_KEY_LEFT_SHIFT,
};

static constexpr int MOUSE_BUTTON_CODES[MOUSE_BUTTON_COUNT] = {
    GLFW_MOUSE_BUTTON_LEFT,
    GLFW_MOUSE_BUTTON_RIGHT,
    GLFW_MOUSE_BUTTON_MIDDLE,
};

void update_input(InputState& input, const platform::Window& window) {
  for (size_t i = 0; i < KEY_COUNT; ++i) {
    input.keys_down[i] = glfwGetKey(window.handle, KEY_CODES[i]) == GLFW_PRESS;
  }
  for (size_t i = 0; i < MOUSE_BUTTON_COUNT; ++i) {
    input.mouse_buttons_down[i] =
        glfwGetMouseButton(window.handle, MOUSE_BUTTON_CODES[i]) == GLFW_PRESS;
  }

  double x = 0.0;
  double y = 0.0;
  glfwGetCursorPos(window.handle, &x, &y);
  input.mouse_dx = static_cast<float>(x - input.cursor_x);
  input.mouse_dy = static_cast<float>(y - input.cursor_y);
  input.cursor_x = x;
  input.cursor_y = y;
}

bool is_key_down(const InputState& input, Key key) {
  return input.keys_down[static_cast<size_t>(key)];
}

bool is_mouse_button_down(const InputState& input, MouseButton button) {
  return input.mouse_buttons_down[static_cast<size_t>(button)];
}

void set_cursor_captured(InputState& input, const platform::Window& window,
                         bool isCaptured) {
  if (isCaptured) {
    glfwSetInputMode(window.handle, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (glfwRawMouseMotionSupported()) {
      glfwSetInputMode(window.handle, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }
  } else {
    if (glfwRawMouseMotionSupported()) {
      glfwSetInputMode(window.handle, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
    }
    glfwSetInputMode(window.handle, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
  }

  // Changing the mode can move the reported cursor. Restart the delta from
  // here so the camera doesn't jump.
  glfwGetCursorPos(window.handle, &input.cursor_x, &input.cursor_y);
  input.mouse_dx = 0.0f;
  input.mouse_dy = 0.0f;
  input.isCursorCaptured = isCaptured;
}

} // namespace input
