#pragma once

#include "render/vulkan/allocator.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>

namespace platform {
struct Window;
}

namespace vulkan {
struct Context;
struct Swapchain;
} // namespace vulkan

namespace core {
struct EngineSettings;
}

namespace camera {
struct Camera;
struct CameraController;
} // namespace camera

namespace gui {

// Dear ImGui (docking) on GLFW + Vulkan dynamic rendering. Draws the whole
// window: a dockspace holding the Engine, Camera, and Scene windows.
// Create in place and don't move it; ImGui keeps a pointer to `ini_path`.
struct Gui {
  VkFormat color_format = VK_FORMAT_UNDEFINED;
  std::string ini_path;
  // True when no layout was saved, so the first frame builds the default one.
  bool isDefaultLayoutNeeded = false;

  // The scene image shown in the Scene window, registered with ImGui.
  VkDescriptorSet scene_texture = VK_NULL_HANDLE;
  // Size the Scene window wants its image to be, in pixels. The owner of the
  // image resizes to match between frames.
  VkExtent2D requested_scene_extent{};
  bool isSceneHovered = false;
};

// Read-only facts shown in the Engine window.
struct EngineInfo {
  const char* gpu_name = "";
  bool isValidationEnabled = false;
  VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
  VkExtent2D scene_extent{};
  vulkan::AllocatorStats memory{};
};

bool create_gui(Gui& gui, const platform::Window& window, const vulkan::Context& context,
                const vulkan::Swapchain& swapchain);
// Waits for the GPU before tearing down.
void destroy_gui(Gui& gui, const vulkan::Context& context);

// Shows `view` (in SHADER_READ_ONLY_OPTIMAL) in the Scene window, replacing
// the previous image. The previous one must no longer be in use by the GPU.
void set_scene_texture(Gui& gui, VkImageView view);

// While the camera has the mouse, ImGui ignores it so hidden-cursor movement
// doesn't hover or click other windows.
void set_mouse_enabled(bool isEnabled);

// One frame: begin_frame, the draw_* windows, then end_frame.
void begin_frame(Gui& gui);
void draw_engine_window(core::EngineSettings& settings, const EngineInfo& info);
void draw_camera_window(camera::Camera& camera, camera::CameraController& controller);
void draw_scene_window(Gui& gui);
void end_frame();

// Draws the finished frame into `target` (COLOR_ATTACHMENT_OPTIMAL), clearing
// it first.
void record_gui(VkCommandBuffer command_buffer, VkImageView target, VkExtent2D extent);

} // namespace gui
