#include "render/vulkan/context.hpp"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstring>
#include <iterator>
#include <print>
#include <vector>

namespace vulkan {

static constexpr uint32_t MIN_API_VERSION = VK_API_VERSION_1_4;
static constexpr const char* VALIDATION_LAYER = "VK_LAYER_KHRONOS_validation";

// ---------------------------------------------------------------------------
// Instance
// ---------------------------------------------------------------------------

static VKAPI_ATTR VkBool32 VKAPI_CALL
debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
               VkDebugUtilsMessageTypeFlagsEXT,
               const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
  const char* label =
      (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ? "error"
                                                                 : "warning";
  std::println(stderr, "[vulkan] {}: {}", label, data->pMessage);
  return VK_FALSE;
}

static VkDebugUtilsMessengerCreateInfoEXT debug_messenger_info() {
  VkDebugUtilsMessengerCreateInfoEXT info{};
  info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  info.pfnUserCallback = debug_callback;
  return info;
}

static bool has_layer(const char* name) {
  uint32_t count = 0;
  vkEnumerateInstanceLayerProperties(&count, nullptr);
  std::vector<VkLayerProperties> layers(count);
  vkEnumerateInstanceLayerProperties(&count, layers.data());

  for (const VkLayerProperties& layer : layers) {
    if (std::strcmp(layer.layerName, name) == 0) {
      return true;
    }
  }
  return false;
}

// Checks extensions from the loader/driver and from the enabled layers.
static bool has_instance_extensions(const std::vector<const char*>& required,
                                    const std::vector<const char*>& layers) {
  std::vector<VkExtensionProperties> available;
  for (size_t i = 0; i <= layers.size(); ++i) {
    const char* layer = i < layers.size() ? layers[i] : nullptr;
    uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(layer, &count, nullptr);
    size_t offset = available.size();
    available.resize(offset + count);
    vkEnumerateInstanceExtensionProperties(layer, &count, available.data() + offset);
  }

  bool isAllFound = true;
  for (const char* name : required) {
    bool isFound = false;
    for (const VkExtensionProperties& extension : available) {
      if (std::strcmp(extension.extensionName, name) == 0) {
        isFound = true;
        break;
      }
    }
    if (!isFound) {
      std::println(stderr, "[vulkan] missing instance extension: {}", name);
      isAllFound = false;
    }
  }
  return isAllFound;
}

static bool create_instance(Context& context, const ContextConfig& config) {
  uint32_t api_version = 0;
  vkEnumerateInstanceVersion(&api_version);
  if (api_version < MIN_API_VERSION) {
    std::println(stderr, "[vulkan] instance version {}.{} is below the required 1.4",
                 VK_API_VERSION_MAJOR(api_version),
                 VK_API_VERSION_MINOR(api_version));
    return false;
  }

  // GLFW reports the surface extensions for the current platform
  // (VK_KHR_win32_surface on Windows, VK_KHR_xlib/xcb/wayland_surface on Linux).
  uint32_t glfw_count = 0;
  const char** glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_count);
  if (!glfw_extensions) {
    std::println(stderr, "[vulkan] GLFW could not determine the required instance extensions");
    return false;
  }

  std::vector<const char*> extensions(glfw_extensions, glfw_extensions + glfw_count);
  std::vector<const char*> layers;

  bool isValidationEnabled = config.isValidationEnabled;
  if (isValidationEnabled && !has_layer(VALIDATION_LAYER)) {
    std::println(stderr,
                 "[vulkan] validation requested but {} is not installed "
                 "(vulkan-validation-layers / the Vulkan SDK)",
                 VALIDATION_LAYER);
    return false;
  }
  if (isValidationEnabled) {
    layers.push_back(VALIDATION_LAYER);
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    extensions.push_back(VK_EXT_LAYER_SETTINGS_EXTENSION_NAME);
  }

  if (!has_instance_extensions(extensions, layers)) {
    return false;
  }

  VkApplicationInfo app_info{};
  app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app_info.pApplicationName = config.app_name;
  app_info.applicationVersion = config.app_version;
  app_info.pEngineName = "Blossom";
  app_info.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
  app_info.apiVersion = MIN_API_VERSION;

  VkInstanceCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  create_info.pApplicationInfo = &app_info;
  create_info.enabledLayerCount = static_cast<uint32_t>(layers.size());
  create_info.ppEnabledLayerNames = layers.data();
  create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  create_info.ppEnabledExtensionNames = extensions.data();

  // Chaining the messenger info also reports problems from
  // vkCreateInstance/vkDestroyInstance.
  VkDebugUtilsMessengerCreateInfoEXT messenger_info = debug_messenger_info();

  const VkBool32 core = VK_TRUE;
  const VkBool32 sync = config.isSyncValidationEnabled ? VK_TRUE : VK_FALSE;
  const VkBool32 best_practices = config.isBestPracticesEnabled ? VK_TRUE : VK_FALSE;
  const VkBool32 gpu = config.isGpuValidationEnabled ? VK_TRUE : VK_FALSE;
  const VkLayerSettingEXT settings[] = {
      {VALIDATION_LAYER, "validate_core", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &core},
      {VALIDATION_LAYER, "validate_sync", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &sync},
      {VALIDATION_LAYER, "validate_best_practices", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1,
       &best_practices},
      {VALIDATION_LAYER, "gpuav_enable", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &gpu},
  };

  VkLayerSettingsCreateInfoEXT layer_settings{};
  layer_settings.sType = VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT;
  layer_settings.pNext = &messenger_info;
  layer_settings.settingCount = static_cast<uint32_t>(std::size(settings));
  layer_settings.pSettings = settings;

  if (isValidationEnabled) {
    create_info.pNext = &layer_settings;
  }

  VkResult result = vkCreateInstance(&create_info, nullptr, &context.instance);
  if (result != VK_SUCCESS) {
    std::println(stderr, "[vulkan] vkCreateInstance failed: {}", static_cast<int>(result));
    context.instance = VK_NULL_HANDLE;
    return false;
  }

  if (isValidationEnabled) {
    auto create_messenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(context.instance, "vkCreateDebugUtilsMessengerEXT"));
    if (!create_messenger ||
        create_messenger(context.instance, &messenger_info, nullptr,
                         &context.debug_messenger) != VK_SUCCESS) {
      std::println(stderr, "[vulkan] failed to create debug messenger");
      context.debug_messenger = VK_NULL_HANDLE;
    }
  }

  return true;
}

// ---------------------------------------------------------------------------
// Surface
// ---------------------------------------------------------------------------

static bool create_surface(Context& context, GLFWwindow* window) {
  VkResult result =
      glfwCreateWindowSurface(context.instance, window, nullptr, &context.surface);
  if (result != VK_SUCCESS) {
    std::println(stderr, "[vulkan] glfwCreateWindowSurface failed: {}",
                 static_cast<int>(result));
    context.surface = VK_NULL_HANDLE;
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Physical device
// ---------------------------------------------------------------------------

static bool has_device_extensions(VkPhysicalDevice physical_device,
                                  const char* device_name) {
  uint32_t count = 0;
  vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> available(count);
  vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count,
                                       available.data());

  bool isAllFound = true;
  for (const char* name : required_extensions()) {
    bool isFound = false;
    for (const VkExtensionProperties& extension : available) {
      if (std::strcmp(extension.extensionName, name) == 0) {
        isFound = true;
        break;
      }
    }
    if (!isFound) {
      std::println(stderr, "[vulkan] {}: missing device extension {}", device_name, name);
      isAllFound = false;
    }
  }
  return isAllFound;
}

static bool find_queue_families(VkPhysicalDevice physical_device,
                                VkSurfaceKHR surface, QueueFamilies& families) {
  uint32_t count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);
  std::vector<VkQueueFamilyProperties> properties(count);
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, properties.data());

  std::vector<VkBool32> present_support(count, VK_FALSE);
  for (uint32_t i = 0; i < count; ++i) {
    vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, i, surface,
                                         &present_support[i]);
  }

  constexpr VkQueueFlags GRAPHICS_COMPUTE = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
  families = {};

  // Prefer one family that does graphics, compute, and present.
  for (uint32_t i = 0; i < count; ++i) {
    if ((properties[i].queueFlags & GRAPHICS_COMPUTE) == GRAPHICS_COMPUTE &&
        present_support[i]) {
      families.graphics = i;
      families.present = i;
      break;
    }
  }

  if (families.graphics == NO_QUEUE_FAMILY) {
    for (uint32_t i = 0; i < count; ++i) {
      if (families.graphics == NO_QUEUE_FAMILY &&
          (properties[i].queueFlags & GRAPHICS_COMPUTE) == GRAPHICS_COMPUTE) {
        families.graphics = i;
      }
      if (families.present == NO_QUEUE_FAMILY && present_support[i]) {
        families.present = i;
      }
    }
  }

  // Async compute: a compute family without graphics.
  for (uint32_t i = 0; i < count; ++i) {
    if ((properties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
        !(properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
      families.compute = i;
      break;
    }
  }
  if (families.compute == NO_QUEUE_FAMILY) {
    families.compute = families.graphics;
  }

  return families.graphics != NO_QUEUE_FAMILY && families.present != NO_QUEUE_FAMILY;
}

static int device_score(const VkPhysicalDeviceProperties& properties) {
  switch (properties.deviceType) {
  case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
    return 3;
  case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
    return 2;
  case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
    return 1;
  default:
    return 0;
  }
}

static bool select_physical_device(Context& context) {
  uint32_t count = 0;
  vkEnumeratePhysicalDevices(context.instance, &count, nullptr);
  if (count == 0) {
    std::println(stderr, "[vulkan] no GPUs with Vulkan support found");
    return false;
  }
  std::vector<VkPhysicalDevice> physical_devices(count);
  vkEnumeratePhysicalDevices(context.instance, &count, physical_devices.data());

  int best_score = -1;
  for (VkPhysicalDevice physical_device : physical_devices) {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical_device, &properties);
    const char* name = properties.deviceName;

    if (properties.apiVersion < MIN_API_VERSION) {
      std::println(stderr, "[vulkan] {}: supports Vulkan {}.{}, needs 1.4", name,
                   VK_API_VERSION_MAJOR(properties.apiVersion),
                   VK_API_VERSION_MINOR(properties.apiVersion));
      continue;
    }
    if (!has_device_extensions(physical_device, name)) {
      continue;
    }

    DeviceFeatures supported;
    DeviceFeatures enabled;
    query_features(physical_device, supported);
    if (!select_features(supported, enabled, name)) {
      continue;
    }

    QueueFamilies families;
    if (!find_queue_families(physical_device, context.surface, families)) {
      std::println(stderr, "[vulkan] {}: no graphics or present queue", name);
      continue;
    }

    int score = device_score(properties);
    if (score > best_score) {
      best_score = score;
      context.physical_device = physical_device;
      context.properties = properties;
      context.queue_families = families;
    }
  }

  if (context.physical_device == VK_NULL_HANDLE) {
    std::println(stderr, "[vulkan] no suitable GPU found");
    return false;
  }

  DeviceFeatures supported;
  query_features(context.physical_device, supported);
  select_features(supported, context.features, context.properties.deviceName);

  std::println("[vulkan] using {}", context.properties.deviceName);
  return true;
}

// ---------------------------------------------------------------------------
// Logical device
// ---------------------------------------------------------------------------

static bool create_device(Context& context) {
  const QueueFamilies& families = context.queue_families;

  std::vector<uint32_t> unique_families = {families.graphics};
  for (uint32_t family : {families.compute, families.present}) {
    bool isListed = false;
    for (uint32_t listed : unique_families) {
      isListed = isListed || listed == family;
    }
    if (!isListed) {
      unique_families.push_back(family);
    }
  }

  float priority = 1.0f;
  std::vector<VkDeviceQueueCreateInfo> queue_infos;
  for (uint32_t family : unique_families) {
    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;
    queue_infos.push_back(queue_info);
  }

  std::span<const char* const> extensions = required_extensions();
  link(context.features);

  VkDeviceCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  create_info.pNext = &context.features.core;
  create_info.queueCreateInfoCount = static_cast<uint32_t>(queue_infos.size());
  create_info.pQueueCreateInfos = queue_infos.data();
  create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  create_info.ppEnabledExtensionNames = extensions.data();

  VkResult result =
      vkCreateDevice(context.physical_device, &create_info, nullptr, &context.device);
  if (result != VK_SUCCESS) {
    std::println(stderr, "[vulkan] vkCreateDevice failed: {}", static_cast<int>(result));
    context.device = VK_NULL_HANDLE;
    return false;
  }

  vkGetDeviceQueue(context.device, families.graphics, 0, &context.graphics_queue);
  vkGetDeviceQueue(context.device, families.compute, 0, &context.compute_queue);
  vkGetDeviceQueue(context.device, families.present, 0, &context.present_queue);
  return true;
}

// ---------------------------------------------------------------------------
// Context
// ---------------------------------------------------------------------------

bool create_context(Context& context, const ContextConfig& config,
                    GLFWwindow* window) {
  if (!create_instance(context, config) || !create_surface(context, window) ||
      !select_physical_device(context) || !create_device(context)) {
    destroy_context(context);
    return false;
  }
  return true;
}

void destroy_context(Context& context) {
  if (context.device != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(context.device);
    vkDestroyDevice(context.device, nullptr);
  }

  if (context.surface != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(context.instance, context.surface, nullptr);
  }

  if (context.debug_messenger != VK_NULL_HANDLE) {
    auto destroy_messenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(context.instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (destroy_messenger) {
      destroy_messenger(context.instance, context.debug_messenger, nullptr);
    }
  }

  if (context.instance != VK_NULL_HANDLE) {
    vkDestroyInstance(context.instance, nullptr);
  }

  context = {};
}

} // namespace vulkan
