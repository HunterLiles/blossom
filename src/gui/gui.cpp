#include "gui/gui.hpp"

#include "camera/camera.hpp"
#include "camera/camera_controller.hpp"
#include "core/settings.hpp"
#include "math/math.hpp"
#include "platform/window.hpp"
#include "render/vulkan/context.hpp"
#include "render/vulkan/swapchain.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <print>

namespace gui {

static constexpr const char* SCENE_WINDOW = "Scene";
static constexpr const char* ENGINE_WINDOW = "Engine";
static constexpr const char* CAMERA_WINDOW = "Camera";
// #222222, the theme's charcoal. The swapchain is UNORM, so sRGB values go in as-is.
static constexpr VkClearColorValue BACKGROUND = {{0x22 / 255.0f, 0x22 / 255.0f, 0x22 / 255.0f, 1.0f}};
// Room for the font atlas plus the scene image and a few spares.
static constexpr uint32_t DESCRIPTOR_POOL_SIZE =
    IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE + 8;

// ---------------------------------------------------------------------------
// Theme: the Miasma.nvim palette from hunter-liles.com (charcoal, parchment,
// fog, muted gold, rust, moss) with square, bordered, terminal-style widgets.
// ---------------------------------------------------------------------------

namespace palette {
inline constexpr uint32_t CHARCOAL = 0x222222; // background
inline constexpr uint32_t INK = 0x1c1c1c;      // code blocks, inputs
inline constexpr uint32_t PARCHMENT = 0xd7c483; // text
inline constexpr uint32_t FOG = 0xc2c2b0;      // muted text
inline constexpr uint32_t GOLD = 0xc9a554;     // primary accent
inline constexpr uint32_t RUST = 0xbb7744;     // secondary accent
inline constexpr uint32_t MOSS = 0x43492a;     // rules and borders
} // namespace palette

static constexpr ImVec4 color(uint32_t rgb, float alpha = 1.0f) {
  return {static_cast<float>((rgb >> 16) & 0xff) / 255.0f,
          static_cast<float>((rgb >> 8) & 0xff) / 255.0f,
          static_cast<float>(rgb & 0xff) / 255.0f, alpha};
}

static void apply_theme() {
  using namespace palette;
  ImGuiStyle& style = ImGui::GetStyle();

  style.WindowRounding = 0.0f;
  style.ChildRounding = 0.0f;
  style.FrameRounding = 0.0f;
  style.PopupRounding = 0.0f;
  style.ScrollbarRounding = 0.0f;
  style.GrabRounding = 0.0f;
  style.TabRounding = 0.0f;
  style.WindowBorderSize = 1.0f;
  style.FrameBorderSize = 1.0f;
  style.PopupBorderSize = 1.0f;
  style.WindowTitleAlign = ImVec2(0.0f, 0.5f);

  ImVec4* colors = style.Colors;
  colors[ImGuiCol_Text] = color(PARCHMENT);
  colors[ImGuiCol_TextDisabled] = color(FOG, 0.55f);
  colors[ImGuiCol_WindowBg] = color(CHARCOAL);
  colors[ImGuiCol_ChildBg] = color(CHARCOAL, 0.0f);
  colors[ImGuiCol_PopupBg] = color(INK);
  colors[ImGuiCol_Border] = color(MOSS);
  colors[ImGuiCol_BorderShadow] = color(0x000000, 0.0f);

  colors[ImGuiCol_FrameBg] = color(INK);
  colors[ImGuiCol_FrameBgHovered] = color(MOSS, 0.6f);
  colors[ImGuiCol_FrameBgActive] = color(MOSS);

  colors[ImGuiCol_TitleBg] = color(INK);
  colors[ImGuiCol_TitleBgActive] = color(INK);
  colors[ImGuiCol_TitleBgCollapsed] = color(INK);
  colors[ImGuiCol_MenuBarBg] = color(INK);

  colors[ImGuiCol_ScrollbarBg] = color(INK);
  colors[ImGuiCol_ScrollbarGrab] = color(MOSS);
  colors[ImGuiCol_ScrollbarGrabHovered] = color(GOLD, 0.6f);
  colors[ImGuiCol_ScrollbarGrabActive] = color(GOLD);

  colors[ImGuiCol_CheckMark] = color(GOLD);
  colors[ImGuiCol_CheckboxSelectedBg] = color(INK);
  colors[ImGuiCol_SliderGrab] = color(GOLD);
  colors[ImGuiCol_SliderGrabActive] = color(RUST);

  colors[ImGuiCol_Button] = color(MOSS, 0.6f);
  colors[ImGuiCol_ButtonHovered] = color(MOSS);
  colors[ImGuiCol_ButtonActive] = color(RUST);

  colors[ImGuiCol_Header] = color(MOSS, 0.6f);
  colors[ImGuiCol_HeaderHovered] = color(MOSS);
  colors[ImGuiCol_HeaderActive] = color(GOLD, 0.5f);

  colors[ImGuiCol_Separator] = color(MOSS);
  colors[ImGuiCol_SeparatorHovered] = color(GOLD, 0.7f);
  colors[ImGuiCol_SeparatorActive] = color(GOLD);

  colors[ImGuiCol_ResizeGrip] = color(MOSS, 0.4f);
  colors[ImGuiCol_ResizeGripHovered] = color(GOLD, 0.6f);
  colors[ImGuiCol_ResizeGripActive] = color(GOLD);

  colors[ImGuiCol_InputTextCursor] = color(PARCHMENT);

  colors[ImGuiCol_Tab] = color(INK);
  colors[ImGuiCol_TabHovered] = color(MOSS);
  colors[ImGuiCol_TabSelected] = color(CHARCOAL);
  colors[ImGuiCol_TabSelectedOverline] = color(GOLD);
  colors[ImGuiCol_TabDimmed] = color(INK);
  colors[ImGuiCol_TabDimmedSelected] = color(CHARCOAL);
  colors[ImGuiCol_TabDimmedSelectedOverline] = color(MOSS);

  colors[ImGuiCol_DockingPreview] = color(GOLD, 0.5f);
  colors[ImGuiCol_DockingEmptyBg] = color(INK);

  colors[ImGuiCol_PlotLines] = color(FOG);
  colors[ImGuiCol_PlotLinesHovered] = color(RUST);
  colors[ImGuiCol_PlotHistogram] = color(GOLD);
  colors[ImGuiCol_PlotHistogramHovered] = color(RUST);

  colors[ImGuiCol_TableHeaderBg] = color(INK);
  colors[ImGuiCol_TableBorderStrong] = color(MOSS);
  colors[ImGuiCol_TableBorderLight] = color(MOSS, 0.5f);
  colors[ImGuiCol_TableRowBg] = color(0x000000, 0.0f);
  colors[ImGuiCol_TableRowBgAlt] = color(PARCHMENT, 0.03f);

  colors[ImGuiCol_TextLink] = color(GOLD);
  colors[ImGuiCol_TextSelectedBg] = color(GOLD, 0.35f);
  colors[ImGuiCol_TreeLines] = color(MOSS);
  colors[ImGuiCol_DragDropTarget] = color(RUST);
  colors[ImGuiCol_DragDropTargetBg] = color(RUST, 0.15f);
  colors[ImGuiCol_UnsavedMarker] = color(RUST);
  colors[ImGuiCol_NavCursor] = color(GOLD);
  colors[ImGuiCol_NavWindowingHighlight] = color(PARCHMENT, 0.7f);
  colors[ImGuiCol_NavWindowingDimBg] = color(0x000000, 0.5f);
  colors[ImGuiCol_ModalWindowDimBg] = color(0x000000, 0.6f);
}

static void check_result(VkResult result) {
  if (result != VK_SUCCESS) {
    std::println(stderr, "[gui] Vulkan error: {}", static_cast<int>(result));
  }
}

bool create_gui(Gui& gui, const platform::Window& window, const vulkan::Context& context,
                const vulkan::Swapchain& swapchain) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;

  // Keep the window layout with the rest of the engine's configuration.
  gui.ini_path = (std::filesystem::path(BLOSSOM_CONFIG_DIR) / "imgui.ini").string();
  gui.isDefaultLayoutNeeded = !std::filesystem::exists(gui.ini_path);
  io.IniFilename = gui.ini_path.c_str();

  apply_theme();

  if (!ImGui_ImplGlfw_InitForVulkan(window.handle, true)) {
    std::println(stderr, "[gui] failed to initialize the GLFW backend");
    ImGui::DestroyContext();
    gui = {};
    return false;
  }

  // Needs the GLFW backend. Scales spacing and fonts for HiDPI displays.
  float scale = ImGui_ImplGlfw_GetContentScaleForWindow(window.handle);
  if (scale > 1.0f) {
    ImGui::GetStyle().ScaleAllSizes(scale);
  }
  ImGui::GetStyle().FontScaleDpi = scale;

  gui.color_format = swapchain.format;

  ImGui_ImplVulkan_InitInfo init_info{};
  init_info.ApiVersion = VK_API_VERSION_1_4;
  init_info.Instance = context.instance;
  init_info.PhysicalDevice = context.physical_device;
  init_info.Device = context.device;
  init_info.QueueFamily = context.queue_families.graphics;
  init_info.Queue = context.graphics_queue;
  init_info.DescriptorPoolSize = DESCRIPTOR_POOL_SIZE;
  init_info.MinImageCount = 2;
  init_info.ImageCount =
      std::max(init_info.MinImageCount, static_cast<uint32_t>(swapchain.images.size()));
  init_info.UseDynamicRendering = true;
  init_info.PipelineInfoMain.PipelineRenderingCreateInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  init_info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
  init_info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats =
      &gui.color_format;
  // Sub-allocate ImGui's buffers in 1 MiB blocks, as best practices asks.
  init_info.MinAllocationSize = 1024 * 1024;
  init_info.CheckVkResultFn = check_result;

  if (!ImGui_ImplVulkan_Init(&init_info)) {
    std::println(stderr, "[gui] failed to initialize the Vulkan backend");
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    gui = {};
    return false;
  }
  return true;
}

void destroy_gui(Gui& gui, const vulkan::Context& context) {
  vkDeviceWaitIdle(context.device);
  if (gui.scene_texture != VK_NULL_HANDLE) {
    ImGui_ImplVulkan_RemoveTexture(gui.scene_texture);
  }
  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  gui = {};
}

void set_scene_texture(Gui& gui, VkImageView view) {
  if (gui.scene_texture != VK_NULL_HANDLE) {
    ImGui_ImplVulkan_RemoveTexture(gui.scene_texture);
  }
  gui.scene_texture =
      ImGui_ImplVulkan_AddTexture(view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void set_mouse_enabled(bool isEnabled) {
  ImGuiIO& io = ImGui::GetIO();
  if (isEnabled) {
    io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
  } else {
    io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
  }
}

// Scene fills the middle; Engine and Camera stack on the right.
static void build_default_layout(ImGuiID dockspace, const ImGuiViewport* viewport) {
  ImGui::DockBuilderRemoveNode(dockspace);
  ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspace, viewport->WorkSize);

  ImGuiID center = dockspace;
  ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.28f, nullptr, &center);
  ImGuiID right_top = right;
  ImGuiID right_bottom =
      ImGui::DockBuilderSplitNode(right_top, ImGuiDir_Down, 0.5f, nullptr, &right_top);

  ImGui::DockBuilderDockWindow(SCENE_WINDOW, center);
  ImGui::DockBuilderDockWindow(ENGINE_WINDOW, right_top);
  ImGui::DockBuilderDockWindow(CAMERA_WINDOW, right_bottom);
  ImGui::DockBuilderFinish(dockspace);
}

void begin_frame(Gui& gui) {
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGuiID dockspace = ImGui::GetID("BlossomDockSpace");
  if (gui.isDefaultLayoutNeeded) {
    build_default_layout(dockspace, viewport);
    gui.isDefaultLayoutNeeded = false;
  }
  ImGui::DockSpaceOverViewport(dockspace, viewport);
}

static const char* present_mode_name(VkPresentModeKHR mode) {
  switch (mode) {
  case VK_PRESENT_MODE_IMMEDIATE_KHR:
    return "Immediate";
  case VK_PRESENT_MODE_MAILBOX_KHR:
    return "Mailbox";
  case VK_PRESENT_MODE_FIFO_KHR:
    return "FIFO";
  case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
    return "FIFO relaxed";
  default:
    return "Other";
  }
}

static double to_mib(VkDeviceSize bytes) {
  return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

void draw_engine_window(core::EngineSettings& settings, const EngineInfo& info) {
  if (ImGui::Begin(ENGINE_WINDOW)) {
    const ImGuiIO& io = ImGui::GetIO();

    ImGui::SeparatorText("Performance");
    ImGui::Text("%.1f FPS  (%.2f ms)", static_cast<double>(io.Framerate),
                1000.0 / static_cast<double>(std::max(io.Framerate, 0.001f)));

    ImGui::SeparatorText("Display");
    ImGui::Checkbox("VSync", &settings.isVsyncEnabled);
    ImGui::Text("Present mode: %s", present_mode_name(info.present_mode));
    ImGui::Text("Scene resolution: %u x %u", info.scene_extent.width,
                info.scene_extent.height);

    ImGui::SeparatorText("Device");
    ImGui::TextWrapped("GPU: %s", info.gpu_name);
    ImGui::Text("Validation: %s", info.isValidationEnabled ? "on" : "off");

    ImGui::SeparatorText("GPU memory");
    ImGui::Text("Allocations: %u", info.memory.allocation_count);
    ImGui::Text("Blocks: %u  (%u dedicated)", info.memory.block_count,
                info.memory.dedicated_count);
    ImGui::Text("Used: %.1f / %.1f MiB", to_mib(info.memory.used_bytes),
                to_mib(info.memory.reserved_bytes));
  }
  ImGui::End();
}

void draw_camera_window(camera::Camera& camera, camera::CameraController& controller) {
  if (ImGui::Begin(CAMERA_WINDOW)) {
    ImGui::SeparatorText("Transform");
    ImGui::DragFloat3("Position", &camera.position.x, 0.05f);

    float yaw = math::degrees(controller.yaw);
    float pitch = math::degrees(controller.pitch);
    bool isRotated = ImGui::DragFloat("Yaw", &yaw, 0.5f, 0.0f, 0.0f, "%.1f deg");
    isRotated |= ImGui::DragFloat("Pitch", &pitch, 0.5f, -89.0f, 89.0f, "%.1f deg");
    if (isRotated) {
      controller.yaw = math::radians(yaw);
      controller.pitch = math::radians(std::clamp(pitch, -89.0f, 89.0f));
      camera.orientation = camera::orientation_from_yaw_pitch(controller.yaw, controller.pitch);
    }

    ImGui::SeparatorText("Projection");
    float fov = math::degrees(camera.fov_y);
    if (ImGui::SliderFloat("Field of view", &fov, 20.0f, 120.0f, "%.0f deg")) {
      camera.fov_y = math::radians(fov);
    }
    ImGui::DragFloat("Near plane", &camera.near_plane, 0.01f, 0.001f,
                     camera.far_plane * 0.5f, "%.3f");
    ImGui::DragFloat("Far plane", &camera.far_plane, 1.0f, camera.near_plane * 2.0f,
                     10000.0f, "%.1f");

    ImGui::SeparatorText("Controls");
    ImGui::DragFloat("Move speed", &controller.move_speed, 0.1f, 0.1f, 100.0f, "%.1f");
    ImGui::DragFloat("Sprint multiplier", &controller.sprint_multiplier, 0.05f, 1.0f, 10.0f,
                     "%.2f");
    ImGui::DragFloat("Look sensitivity", &controller.look_sensitivity, 0.0001f, 0.0001f,
                     0.02f, "%.4f");
    ImGui::TextDisabled("Hold right mouse over Scene to fly:");
    ImGui::TextDisabled("WASD move, Space/Ctrl up/down, Shift sprint");
  }
  ImGui::End();
}

void draw_scene_window(Gui& gui) {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  bool isVisible = ImGui::Begin(SCENE_WINDOW, nullptr,
                                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar();

  gui.isSceneHovered = false;
  if (isVisible) {
    ImVec2 size = ImGui::GetContentRegionAvail();
    ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
    if (size.x >= 1.0f && size.y >= 1.0f) {
      gui.requested_scene_extent = {static_cast<uint32_t>(size.x * scale.x),
                                    static_cast<uint32_t>(size.y * scale.y)};
      if (gui.scene_texture != VK_NULL_HANDLE) {
        ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(gui.scene_texture)),
                     size);
      }
    }
    gui.isSceneHovered = ImGui::IsWindowHovered();
  }
  ImGui::End();
}

void end_frame() { ImGui::Render(); }

void record_gui(VkCommandBuffer command_buffer, VkImageView target, VkExtent2D extent) {
  VkRenderingAttachmentInfo color_attachment{};
  color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  color_attachment.imageView = target;
  color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color_attachment.clearValue.color = BACKGROUND;

  VkRenderingInfo rendering{};
  rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  rendering.renderArea = {{0, 0}, extent};
  rendering.layerCount = 1;
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachments = &color_attachment;

  vkCmdBeginRendering(command_buffer, &rendering);
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command_buffer);
  vkCmdEndRendering(command_buffer);
}

} // namespace gui
