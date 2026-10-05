#include "Input.hpp"

#include "Scene.hpp"

#include <GLFW/glfw3.h>

namespace {

bool keyDown(GLFWwindow *window, int key) {
  return glfwGetKey(window, key) == GLFW_PRESS;
}

void processExitKeys(Input &input, GLFWwindow *window) {
  const bool escapeDown = keyDown(window, GLFW_KEY_ESCAPE);
  if (escapeDown && !input.escapeWasDown) {
    if (input.exitConfirmationOpen) {
      cancelExitConfirmation(input, window);
    } else {
      input.exitConfirmationOpen = true;
      input.resumeInputAfterExitConfirmation = input.cursorCaptured;
      if (input.cursorCaptured) {
        setCursorCapture(input, window, false);
      }
    }
  }
  input.escapeWasDown = escapeDown;
  const bool enterDown =
      keyDown(window, GLFW_KEY_ENTER) || keyDown(window, GLFW_KEY_KP_ENTER);
  if (input.exitConfirmationOpen && enterDown && !input.enterWasDown) {
    glfwSetWindowShouldClose(window, GLFW_TRUE);
  }
  input.enterWasDown = enterDown;
}

bool processShortcuts(Input &input, GLFWwindow *window) {
  const bool tabDown = keyDown(window, GLFW_KEY_TAB);
  if (tabDown && !input.tabWasDown) {
    setCursorCapture(input, window, !input.cursorCaptured);
  }
  input.tabWasDown = tabDown;

  const bool shaderReloadDown = keyDown(window, GLFW_KEY_F5);
  const bool reloadRequested = shaderReloadDown && !input.shaderReloadWasDown;
  input.shaderReloadWasDown = shaderReloadDown;
  return reloadRequested;
}

void rotateCamera(Input &input, GLFWwindow *window, Camera &camera) {
  double mouseX = 0.0;
  double mouseY = 0.0;
  glfwGetCursorPos(window, &mouseX, &mouseY);
  if (!input.firstMouse) {
    const float yaw = static_cast<float>(input.mouseX - mouseX) * camera.sensitivity;
    const float pitch = static_cast<float>(input.mouseY - mouseY) * camera.sensitivity;
    const math::Quaternion yawRotation = math::axisAngle({0.0f, 1.0f, 0.0f}, yaw);
    const math::Quaternion pitchRotation = math::axisAngle({1.0f, 0.0f, 0.0f}, pitch);
    const math::Quaternion pitched = math::multiply(camera.orientation, pitchRotation);
    camera.orientation = math::normalize(math::multiply(yawRotation, pitched));
  }
  input.mouseX = mouseX;
  input.mouseY = mouseY;
  input.firstMouse = false;
}

void moveCamera(GLFWwindow *window, Camera &camera, float deltaTime) {
  math::Vec3 movement{};
  const math::Vec3 forward = math::rotate(camera.orientation, {0.0f, 0.0f, -1.0f});
  const math::Vec3 right = math::rotate(camera.orientation, {1.0f, 0.0f, 0.0f});
  if (keyDown(window, GLFW_KEY_W)) {
    movement = math::add(movement, forward);
  }
  if (keyDown(window, GLFW_KEY_S)) {
    movement = math::subtract(movement, forward);
  }
  if (keyDown(window, GLFW_KEY_D)) {
    movement = math::add(movement, right);
  }
  if (keyDown(window, GLFW_KEY_A)) {
    movement = math::subtract(movement, right);
  }
  if (keyDown(window, GLFW_KEY_SPACE)) {
    movement.y += 1.0f;
  }
  if (keyDown(window, GLFW_KEY_LEFT_CONTROL) || keyDown(window, GLFW_KEY_RIGHT_CONTROL)) {
    movement.y -= 1.0f;
  }
  const float speed =
      keyDown(window, GLFW_KEY_LEFT_SHIFT) ? camera.speed * 2.5f : camera.speed;
  const math::Vec3 displacement =
      math::scale(math::normalize(movement), speed * deltaTime);
  camera.position = math::add(camera.position, displacement);
}

} // namespace

void cancelExitConfirmation(Input &input, GLFWwindow *window) {
  input.exitConfirmationOpen = false;
  if (input.resumeInputAfterExitConfirmation) {
    setCursorCapture(input, window, true);
  }
  input.resumeInputAfterExitConfirmation = false;
}

void setCursorCapture(Input &input, GLFWwindow *window, bool captured) {
  input.cursorCaptured = captured;
  input.firstMouse = true;
  // Disabled cursors provide relative mouse motion, rather than screen edges.
  glfwSetInputMode(
      window, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
  if (glfwRawMouseMotionSupported()) {
    glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, captured ? GLFW_TRUE : GLFW_FALSE);
  }
}

bool processInput(Input &input, GLFWwindow *window, Scene &scene, float deltaTime) {
  processExitKeys(input, window);
  if (input.exitConfirmationOpen) {
    return false;
  }

  const bool reloadRequested = processShortcuts(input, window);
  if (input.cursorCaptured) {
    Camera &camera = focusedCamera(scene);
    rotateCamera(input, window, camera);
    moveCamera(window, camera, deltaTime);
  }
  return reloadRequested;
}
