#pragma once

#include <cstddef>

namespace platform {
struct Window;
}

namespace input {

enum class Key {
  W,
  A,
  S,
  D,
  Space,
  LeftControl,
  LeftShift,
  Count,
};

enum class MouseButton {
  Left,
  Right,
  Middle,
  Count,
};

inline constexpr size_t KEY_COUNT = static_cast<size_t>(Key::Count);
inline constexpr size_t MOUSE_BUTTON_COUNT = static_cast<size_t>(MouseButton::Count);

// Snapshot of the devices for one frame. Owned by the input system and
// read by everything else.
struct InputState {
  bool keys_down[KEY_COUNT]{};
  bool mouse_buttons_down[MOUSE_BUTTON_COUNT]{};
  double cursor_x = 0.0;
  double cursor_y = 0.0;
  // Cursor movement since the last update, in pixels. +Y is down.
  float mouse_dx = 0.0f;
  float mouse_dy = 0.0f;
  bool isCursorCaptured = false;
};

// Call once per frame after platform::poll_events().
void update_input(InputState& input, const platform::Window& window);

bool is_key_down(const InputState& input, Key key);
bool is_mouse_button_down(const InputState& input, MouseButton button);

// Captured: the cursor is hidden and held in the window while movement is
// still reported through mouse_dx/mouse_dy (raw, unaccelerated when supported).
// Released: the cursor is shown again.
void set_cursor_captured(InputState& input, const platform::Window& window,
                         bool isCaptured);

} // namespace input
