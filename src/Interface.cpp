#include "Interface.hpp"

#include "Input.hpp"
#include "Renderer.hpp"
#include "Scene.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <cmath>

namespace {

constexpr ImGuiWindowFlags lockedLayoutWindowFlags =
    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

bool sceneCubeWasClicked(const Scene &scene, ImVec2 imageOrigin, ImVec2 imageSize) {
  if (!ImGui::IsItemClicked()) {
    return false;
  }

  const Camera &camera = scene.sceneCamera;
  const math::Vec3 forward = math::rotate(camera.orientation, {0.0f, 0.0f, -1.0f});
  const math::Vec3 up = math::rotate(camera.orientation, {0.0f, 1.0f, 0.0f});
  const math::Mat4 view = math::lookAt(camera.position, forward, up);
  const float aspect = imageSize.x / std::max(imageSize.y, 1.0f);
  const math::Mat4 projection =
      math::perspective(math::radians(camera.fieldOfView), aspect, 0.1f, 100.0f);
  const math::Mat4 model = math::translation(scene.cube.position);
  const math::Mat4 modelViewProjection =
      math::multiply(math::multiply(model, view), projection);

  const float clipX = modelViewProjection.value[12];
  const float clipY = modelViewProjection.value[13];
  const float clipW = modelViewProjection.value[15];
  if (clipW <= 0.0f) {
    return false;
  }

  // Approximate picking with a circle around the projected cube center.
  const ImVec2 center{imageOrigin.x + (clipX / clipW * 0.5f + 0.5f) * imageSize.x,
                      imageOrigin.y + (-clipY / clipW * 0.5f + 0.5f) * imageSize.y};
  const float radius = std::max(18.0f, std::abs(0.5f / clipW) * imageSize.x);
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  const float dx = mouse.x - center.x;
  const float dy = mouse.y - center.y;
  return dx * dx + dy * dy <= radius * radius;
}

void drawControls(Input &input, GLFWwindow *window, Scene &scene) {
  if (ImGui::Begin("Controls", nullptr, lockedLayoutWindowFlags)) {
    Camera &camera = focusedCamera(scene);
    ImGui::TextUnformatted("WASD: move camera   Space/Ctrl: vertical");
    ImGui::TextUnformatted("Escape: exit");
    if (ImGui::Checkbox("Hide cursor and capture focused view (Tab)",
                        &input.cursorCaptured)) {
      setCursorCapture(input, window, input.cursorCaptured);
    }
    ImGui::Text("Input target: %s",
                scene.focusedView == ViewKind::Scene ? "Scene" : "Game");

    ImGui::SliderFloat(
        "Field of view", &camera.fieldOfView, 30.0f, 120.0f, "%.0f degrees");
    ImGui::SliderFloat("Camera speed", &camera.speed, 0.25f, 20.0f, "%.2f");
    ImGui::SliderFloat("Camera sensitivity", &camera.sensitivity, 0.0005f, 0.01f, "%.4f");

    ImGui::Checkbox("Global ambient light", &scene.globalLight.enabled);
    ImGui::SliderFloat(
        "Global light level", &scene.globalLight.level, 0.0f, 2.0f, "%.2f");

    ImGui::SeparatorText("Transform");
    ImGui::DragFloat3("Cube position", &scene.cube.position.x, 0.02f);
  }
  ImGui::End();
}

void drawCameraPanel(const Input &input, Scene &scene) {
  if (ImGui::Begin("Camera", nullptr, lockedLayoutWindowFlags)) {
    Camera &camera = focusedCamera(scene);
    ImGui::Text("Active: %s", scene.focusedView == ViewKind::Scene ? "Scene" : "Game");
    ImGui::Text("Position: %.2f, %.2f, %.2f",
                camera.position.x,
                camera.position.y,
                camera.position.z);
    ImGui::Text("Orientation: %.2f, %.2f, %.2f, %.2f",
                camera.orientation.x,
                camera.orientation.y,
                camera.orientation.z,
                camera.orientation.w);

    if (ImGui::Button("Reset active camera")) {
      camera =
          scene.focusedView == ViewKind::Scene ? Camera{{0.0f, 1.5f, 5.0f}} : Camera{};
    }
    ImGui::TextUnformatted(input.cursorCaptured ? "Mouse captured"
                                                : "Mouse released for UI");
  }
  ImGui::End();
}

bool drawEnginePanel(Interface &interface, const Renderer &renderer, float frameTime) {
  bool reloadRequested = false;
  if (ImGui::Begin("Engine", nullptr, lockedLayoutWindowFlags)) {
    drawWorkspaceLayoutSelector(interface);
    ImGui::Separator();

    ImGui::Text("Frame rate: %.1f FPS", ImGui::GetIO().Framerate);
    ImGui::Text("Frame time: %.2f ms", frameTime * 1000.0f);
    ImGui::Text("Swapchain: %u x %u",
                renderer.swapchainExtent.width,
                renderer.swapchainExtent.height);
    ImGui::Text("Images: %zu", renderer.swapchainImages.size());

    if (ImGui::Button("Reload shaders (F5)")) {
      reloadRequested = true;
    }
    ImGui::TextWrapped("%s", renderer.shaderReloadStatus.c_str());
  }
  ImGui::End();
  return reloadRequested;
}

void drawView(Interface &interface, Scene &scene, Renderer &renderer, ViewKind kind) {
  const bool sceneView = kind == ViewKind::Scene;
  if (ImGui::Begin(sceneView ? "Scene" : "Game", nullptr, lockedLayoutWindowFlags)) {
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
      scene.focusedView = kind;
    }
    ImGui::TextUnformatted(
        sceneView
            ? "Editor view: click the cube to inspect it. Runtime transformations are "
              "paused."
            : "Runtime view: camera controls and cube animation are active.");

    ViewTarget &target = renderer.views[static_cast<uint32_t>(kind)];
    const ImVec2 available = ImGui::GetContentRegionAvail();
    target.requestedExtent = {static_cast<uint32_t>(std::max(64.0f, available.x)),
                              static_cast<uint32_t>(std::max(64.0f, available.y))};
    const ImTextureRef texture{
        static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(target.texture))};
    ImGui::Image(texture, available);

    if (sceneView && sceneCubeWasClicked(scene, ImGui::GetItemRectMin(), available)) {
      scene.selectedObject = SceneObject::Cube;
      interface.inspectorOpen = true;
    }
  }
  ImGui::End();
}

void drawInspector(Interface &interface, Scene &scene) {
  if (!interface.inspectorOpen || scene.selectedObject != SceneObject::Cube) {
    return;
  }

  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  const ImVec2 initialPosition{viewport->WorkPos.x +
                                   std::max(0.0f, viewport->WorkSize.x - 316.0f),
                               viewport->WorkPos.y + 16.0f};
  ImGui::SetNextWindowPos(initialPosition, ImGuiCond_Once);
  ImGui::SetNextWindowSize({300.0f, 140.0f}, ImGuiCond_Always);
  ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
  const ImGuiWindowFlags inspectorFlags =
      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
  if (ImGui::Begin("Inspector: Cube", &interface.inspectorOpen, inspectorFlags)) {
    ImGui::TextUnformatted("Cube");
    ImGui::DragFloat3("Position", &scene.cube.position.x, 0.02f);
  }
  ImGui::End();
}

void drawExitConfirmation(Input &input, GLFWwindow *window) {
  if (input.exitConfirmationOpen) {
    ImGui::OpenPopup("Exit Blossom?");
  }
  if (ImGui::BeginPopupModal(
          "Exit Blossom?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (!input.exitConfirmationOpen) {
      ImGui::CloseCurrentPopup();
    } else {
      ImGui::TextUnformatted("Exit the application?");
      ImGui::TextUnformatted("Enter: Yes    Escape: No");
      if (ImGui::Button("Yes", {120.0f, 0.0f})) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
      }
      ImGui::SameLine();
      if (ImGui::Button("No", {120.0f, 0.0f})) {
        cancelExitConfirmation(input, window);
        ImGui::CloseCurrentPopup();
      }
      ImGui::SetItemDefaultFocus();
    }
    ImGui::EndPopup();
  }
}

} // namespace

bool buildInterface(Interface &interface,
                    Input &input,
                    Scene &scene,
                    Renderer &renderer,
                    float frameTime) {
  if (interface.layoutChangeRequested) {
    loadWorkspaceLayout(interface);
  }

  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  beginWorkspaceDockspace();
  drawControls(input, renderer.window, scene);
  drawCameraPanel(input, scene);
  const bool reloadRequested = drawEnginePanel(interface, renderer, frameTime);
  drawView(interface, scene, renderer, ViewKind::Scene);
  drawView(interface, scene, renderer, ViewKind::Game);
  drawInspector(interface, scene);
  drawExitConfirmation(input, renderer.window);

  ImGui::Render();
  return reloadRequested;
}
