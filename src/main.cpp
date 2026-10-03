#include "Input.hpp"
#include "Interface.hpp"
#include "Renderer.hpp"
#include "Scene.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <iostream>

int main() {
  Renderer renderer{};
  Scene scene{};
  Input input{};
  Interface interface{};

  try {
    initializeRenderer(renderer);
    loadWorkspaceLayout(interface);
    double previousTime = glfwGetTime();

    while (!glfwWindowShouldClose(renderer.window)) {
      glfwPollEvents();

      const double now = glfwGetTime();
      const float deltaTime = static_cast<float>(std::min(now - previousTime, 0.1));
      previousTime = now;

      const bool keyboardReload = processInput(input, renderer.window, scene, deltaTime);
      if (glfwWindowShouldClose(renderer.window)) {
        break;
      }
      if (!input.exitConfirmationOpen) {
        scene.cube.angle += deltaTime * 0.65f;
      }

      resizeViewTargets(renderer);
      const bool interfaceReload =
          buildInterface(interface, input, scene, renderer, deltaTime);
      pollShaderReload(renderer, now, keyboardReload || interfaceReload);
      drawFrame(renderer, scene);
    }
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    shutdownRenderer(renderer);
    return EXIT_FAILURE;
  }

  shutdownRenderer(renderer);
  return EXIT_SUCCESS;
}
