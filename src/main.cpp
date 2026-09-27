#include "camera/camera.hpp"
#include "camera/camera_controller.hpp"
#include "core/settings.hpp"
#include "gui/gui.hpp"
#include "input/input.hpp"
#include "math/math.hpp"
#include "platform/time.hpp"
#include "platform/window.hpp"
#include "render/raster/renderer.hpp"
#include "render/vulkan/allocator.hpp"
#include "render/vulkan/context.hpp"
#include "render/vulkan/frame.hpp"
#include "render/vulkan/swapchain.hpp"
#include "scene/scene.hpp"

#include <algorithm>

int main() {
  if (!platform::init()) {
    return 1;
  }

  platform::Window window{};
  if (!platform::create_window(window, platform::WindowConfig{})) {
    platform::shutdown();
    return 1;
  }

  vulkan::ContextConfig context_config{};
#ifndef NDEBUG
  context_config.isValidationEnabled = true;
#endif

  vulkan::Context context{};
  if (!vulkan::create_context(context, context_config, window.handle)) {
    platform::destroy_window(window);
    platform::shutdown();
    return 1;
  }

  vulkan::Allocator allocator{};
  vulkan::create_allocator(allocator, context, vulkan::AllocatorConfig{});

  core::EngineSettings settings{};

  vulkan::SwapchainConfig swapchain_config{};
  swapchain_config.isVsyncEnabled = settings.isVsyncEnabled;
  platform::framebuffer_size(window, swapchain_config.extent.width,
                             swapchain_config.extent.height);

  vulkan::Swapchain swapchain{};
  vulkan::FrameLoop frame_loop{};
  if (!vulkan::create_swapchain(swapchain, context, swapchain_config) ||
      !vulkan::create_frame_loop(frame_loop, context, swapchain)) {
    vulkan::destroy_swapchain(swapchain, context);
    vulkan::destroy_allocator(allocator, context);
    vulkan::destroy_context(context);
    platform::destroy_window(window);
    platform::shutdown();
    return 1;
  }

  scene::Scene scene = scene::make_test_scene();

  camera::Camera camera{};
  camera.position = {0.0f, 1.2f, -4.0f};
  camera::look_at(camera, {0.0f, 0.0f, 0.0f});
  camera::CameraController camera_controller = camera::make_controller(camera);

  input::InputState input{};

  // Starts at the window size; the Scene window resizes it on the first frame.
  raster::Renderer renderer{};
  gui::Gui gui{};
  if (!raster::create_renderer(renderer, context, allocator, swapchain.extent) ||
      !raster::upload_scene(renderer, context, allocator, scene) ||
      !gui::create_gui(gui, window, context, swapchain)) {
    raster::destroy_renderer(renderer, context, allocator);
    vulkan::destroy_frame_loop(frame_loop, context);
    vulkan::destroy_swapchain(swapchain, context);
    vulkan::destroy_allocator(allocator, context);
    vulkan::destroy_context(context);
    platform::destroy_window(window);
    platform::shutdown();
    return 1;
  }
  gui::set_scene_texture(gui, renderer.display_view);

  int exit_code = 0;
  bool isSwapchainDirty = false;
  double last_time = platform::time_seconds();

  while (!platform::should_close(window)) {
    platform::poll_events();
    input::update_input(input, window);

    // Clamped so a stall (resize, breakpoint) doesn't teleport the camera.
    double now = platform::time_seconds();
    float delta_seconds = std::min(static_cast<float>(now - last_time), 0.1f);
    last_time = now;

    uint32_t width = 0;
    uint32_t height = 0;
    platform::framebuffer_size(window, width, height);
    if (width == 0 || height == 0) {
      continue; // Minimized.
    }

    // Swapchain: window size and VSync.
    if (width != swapchain_config.extent.width || height != swapchain_config.extent.height ||
        settings.isVsyncEnabled != swapchain_config.isVsyncEnabled) {
      isSwapchainDirty = true;
    }
    if (isSwapchainDirty) {
      swapchain_config.extent = {width, height};
      swapchain_config.isVsyncEnabled = settings.isVsyncEnabled;
      if (!vulkan::create_swapchain(swapchain, context, swapchain_config) ||
          !vulkan::resize_frame_loop(frame_loop, context, swapchain)) {
        exit_code = 1;
        break;
      }
      isSwapchainDirty = false;
    }

    // Scene view: the size the Scene window asked for last frame. Done before
    // the GUI frame so ImGui never draws with a destroyed image.
    VkExtent2D requested = gui.requested_scene_extent;
    if (requested.width > 0 && requested.height > 0 &&
        (requested.width != renderer.extent.width ||
         requested.height != renderer.extent.height)) {
      if (!raster::resize_target(renderer, context, allocator, requested)) {
        exit_code = 1;
        break;
      }
      gui::set_scene_texture(gui, renderer.display_view);
    }

    gui::EngineInfo engine_info{};
    engine_info.gpu_name = context.properties.deviceName;
    engine_info.isValidationEnabled = context_config.isValidationEnabled;
    engine_info.present_mode = swapchain.present_mode;
    engine_info.scene_extent = renderer.extent;
    engine_info.memory = vulkan::allocator_stats(allocator);

    gui::begin_frame(gui);
    gui::draw_scene_window(gui);
    gui::draw_engine_window(settings, engine_info);
    gui::draw_camera_window(camera, camera_controller);
    gui::end_frame();

    camera::update_controller(camera_controller, camera, input, window, gui.isSceneHovered,
                              delta_seconds);
    gui::set_mouse_enabled(!camera_controller.isActive);

    float aspect = static_cast<float>(renderer.extent.width) /
                   static_cast<float>(renderer.extent.height);
    math::Mat4 view_projection =
        camera::projection_matrix(camera, aspect) * camera::view_matrix(camera);

    vulkan::Frame frame{};
    vulkan::SwapchainStatus status = vulkan::begin_frame(frame_loop, context, swapchain, frame);
    if (status == vulkan::SwapchainStatus::OutOfDate) {
      isSwapchainDirty = true;
      continue;
    }
    if (status == vulkan::SwapchainStatus::Error) {
      exit_code = 1;
      break;
    }

    raster::record_scene(renderer, frame.command_buffer, scene, view_projection);
    gui::record_gui(frame.command_buffer, swapchain.image_views[frame.image_index],
                    swapchain.extent);

    status = vulkan::end_frame(frame_loop, context, swapchain, frame);
    if (status == vulkan::SwapchainStatus::OutOfDate ||
        status == vulkan::SwapchainStatus::Suboptimal) {
      isSwapchainDirty = true;
    } else if (status == vulkan::SwapchainStatus::Error) {
      exit_code = 1;
      break;
    }
  }

  gui::destroy_gui(gui, context);
  raster::destroy_renderer(renderer, context, allocator);
  vulkan::destroy_frame_loop(frame_loop, context);
  vulkan::destroy_swapchain(swapchain, context);
  vulkan::destroy_allocator(allocator, context);
  vulkan::destroy_context(context);
  platform::destroy_window(window);
  platform::shutdown();
  return exit_code;
}
