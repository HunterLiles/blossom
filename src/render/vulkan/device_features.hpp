#pragma once

#include <vulkan/vulkan.h>

#include <span>

namespace vulkan {

// Feature chain for Vulkan 1.0 through 1.4. The structs point at each other
// through pNext, so call link() on any copy before handing it to Vulkan.
struct DeviceFeatures {
  VkPhysicalDeviceFeatures2 core{};
  VkPhysicalDeviceVulkan11Features vulkan11{};
  VkPhysicalDeviceVulkan12Features vulkan12{};
  VkPhysicalDeviceVulkan13Features vulkan13{};
  VkPhysicalDeviceVulkan14Features vulkan14{};
};

void link(DeviceFeatures& features);
void query_features(VkPhysicalDevice physical_device, DeviceFeatures& features);

// Fills `enabled` with every required feature plus every optional feature
// `supported` has. Returns false and logs each missing required feature.
bool select_features(const DeviceFeatures& supported, DeviceFeatures& enabled,
                     const char* device_name);

std::span<const char* const> required_extensions();

} // namespace vulkan
