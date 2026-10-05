#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct GLFWwindow;
struct Scene;

inline constexpr uint32_t framesInFlight = 2;
inline constexpr uint32_t viewCount = 2;
inline constexpr uint32_t shaderFileCount = 4;
inline constexpr VkExtent2D editorViewExtent{640, 360};

struct QueueFamilies {
  uint32_t graphics = UINT32_MAX;
  uint32_t present = UINT32_MAX;
};

struct FrameSync {
  VkSemaphore imageAvailable = VK_NULL_HANDLE;
  VkSemaphore renderFinished = VK_NULL_HANDLE;
  VkFence finished = VK_NULL_HANDLE;
};

struct DepthTarget {
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
};

struct ViewTarget {
  VkImage colorImage = VK_NULL_HANDLE;
  VkDeviceMemory colorMemory = VK_NULL_HANDLE;
  VkImageView colorView = VK_NULL_HANDLE;
  VkImage depthImage = VK_NULL_HANDLE;
  VkDeviceMemory depthMemory = VK_NULL_HANDLE;
  VkImageView depthView = VK_NULL_HANDLE;
  VkFramebuffer framebuffer = VK_NULL_HANDLE;
  VkDescriptorSet texture = VK_NULL_HANDLE;
  VkExtent2D extent = editorViewExtent;
  VkExtent2D requestedExtent = editorViewExtent;
};

struct ShaderFileState {
  std::filesystem::file_time_type modified{};
  bool exists = false;

  bool operator==(const ShaderFileState &) const = default;
};

struct SceneUniform {
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  void *mapped = nullptr;
  VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
};

struct Renderer {
  // Window and shader reload.
  GLFWwindow *window = nullptr;
  bool glfwStarted = false;
  bool framebufferResized = false;
  double lastShaderPollTime = 0.0;
  std::array<ShaderFileState, shaderFileCount> shaderFiles{};
  std::string shaderReloadStatus = "Waiting for shader changes";

  // Vulkan instance and device.
  VkInstance instance = VK_NULL_HANDLE;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  QueueFamilies queues{};
  VkDevice device = VK_NULL_HANDLE;
  VkQueue graphicsQueue = VK_NULL_HANDLE;
  VkQueue presentQueue = VK_NULL_HANDLE;

  // Swapchain and render targets.
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
  VkExtent2D swapchainExtent{};
  std::vector<VkImage> swapchainImages;
  std::vector<VkImageView> imageViews;
  std::vector<DepthTarget> depthTargets;
  VkFormat depthFormat = VK_FORMAT_UNDEFINED;
  VkRenderPass renderPass = VK_NULL_HANDLE;
  VkRenderPass viewRenderPass = VK_NULL_HANDLE;
  VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
  VkPipeline cubePipeline = VK_NULL_HANDLE;
  VkPipeline gridPipeline = VK_NULL_HANDLE;
  std::vector<VkFramebuffer> framebuffers;
  std::array<ViewTarget, viewCount> views{};

  // Commands and synchronization.
  VkCommandPool commandPool = VK_NULL_HANDLE;
  std::vector<VkCommandBuffer> commandBuffers;
  std::array<FrameSync, framesInFlight> frames{};
  uint32_t frameIndex = 0;

  // Per-frame, per-view scene uniforms.
  VkDescriptorSetLayout sceneSetLayout = VK_NULL_HANDLE;
  VkDescriptorPool sceneDescriptorPool = VK_NULL_HANDLE;
  std::array<std::array<SceneUniform, viewCount>, framesInFlight> sceneUniforms{};

  // ImGui backend lifecycle, including partially completed initialization.
  VkDescriptorPool imguiDescriptorPool = VK_NULL_HANDLE;
  bool imguiGlfwReady = false;
  bool imguiReady = false;
};

void initializeRenderer(Renderer &renderer);
void shutdownRenderer(Renderer &renderer);
void pollShaderReload(Renderer &renderer, double now, bool requested);
void resizeViewTargets(Renderer &renderer);
void drawFrame(Renderer &renderer, const Scene &scene);
