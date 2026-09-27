#pragma once

#include "render/vulkan/device_features.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>

struct GLFWwindow;

namespace vulkan {

inline constexpr uint32_t NO_QUEUE_FAMILY = UINT32_MAX;

struct ContextConfig {
  const char* app_name = "Blossom";
  uint32_t app_version = VK_MAKE_API_VERSION(0, 0, 1, 0);

  // Requires VK_LAYER_KHRONOS_validation; context creation fails without it.
  // Core API-usage checks are always on when validation is enabled. The
  // flags below add the optional checks and only apply with validation on.
  bool isValidationEnabled = false;
  // Missing barriers and read/write hazards between commands and submits.
  bool isSyncValidationEnabled = true;
  // Legal but inefficient or risky usage, reported as warnings.
  bool isBestPracticesEnabled = true;
  // Instruments shaders to catch out-of-bounds descriptor, buffer, and
  // buffer-device-address access on the GPU. Off by default: it is very slow
  // alongside core checks and force-enables extra device features, so Debug
  // would no longer match Release. Turn on when hunting shader memory bugs.
  bool isGpuValidationEnabled = false;
};

// Compute is a separate family when the GPU has an async compute queue,
// otherwise it matches graphics.
struct QueueFamilies {
  uint32_t graphics = NO_QUEUE_FAMILY;
  uint32_t compute = NO_QUEUE_FAMILY;
  uint32_t present = NO_QUEUE_FAMILY;
};

// Create in place and don't copy: `features` holds pointers into itself.
struct Context {
  VkInstance instance = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
  VkSurfaceKHR surface = VK_NULL_HANDLE;

  VkPhysicalDevice physical_device = VK_NULL_HANDLE;
  VkPhysicalDeviceProperties properties{};
  DeviceFeatures features{};
  QueueFamilies queue_families{};

  VkDevice device = VK_NULL_HANDLE;
  VkQueue graphics_queue = VK_NULL_HANDLE;
  VkQueue compute_queue = VK_NULL_HANDLE;
  VkQueue present_queue = VK_NULL_HANDLE;
};

bool create_context(Context& context, const ContextConfig& config,
                    GLFWwindow* window);
void destroy_context(Context& context);

} // namespace vulkan
