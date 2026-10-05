#pragma once

struct GLFWwindow;
struct Scene;

struct Input {
  bool cursorCaptured = false;
  bool firstMouse = true;
  bool tabWasDown = false;
  bool shaderReloadWasDown = false;
  bool escapeWasDown = false;
  bool enterWasDown = false;
  bool exitConfirmationOpen = false;
  bool resumeInputAfterExitConfirmation = false;
  double mouseX = 0.0;
  double mouseY = 0.0;
};

void setCursorCapture(Input &input, GLFWwindow *window, bool captured);
void cancelExitConfirmation(Input &input, GLFWwindow *window);
// Returns a shader reload request from the keyboard.
bool processInput(Input &input, GLFWwindow *window, Scene &scene, float deltaTime);
