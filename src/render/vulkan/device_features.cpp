#include "render/vulkan/device_features.hpp"

#include <cstdio>
#include <print>

namespace vulkan {

namespace {

struct Feature {
  const char* name;
  VkBool32& (*get)(DeviceFeatures&);
  const VkBool32& (*read)(const DeviceFeatures&);
};

} // namespace

#define FEATURE(member)                                                        \
  Feature {                                                                    \
    #member, [](DeviceFeatures& f) -> VkBool32& { return f.member; },          \
        [](const DeviceFeatures& f) -> const VkBool32& { return f.member; }    \
  }

static constexpr Feature REQUIRED_FEATURES[] = {
    FEATURE(vulkan12.timelineSemaphore),
    FEATURE(vulkan12.bufferDeviceAddress),
    FEATURE(vulkan12.descriptorIndexing),
    FEATURE(vulkan12.runtimeDescriptorArray),
    FEATURE(vulkan12.descriptorBindingPartiallyBound),
    FEATURE(vulkan12.scalarBlockLayout),
    FEATURE(vulkan12.hostQueryReset),
    FEATURE(vulkan13.dynamicRendering),
    FEATURE(vulkan13.synchronization2),
};

static constexpr Feature OPTIONAL_FEATURES[] = {
    FEATURE(core.features.samplerAnisotropy),
    FEATURE(core.features.pipelineStatisticsQuery),
    FEATURE(core.features.shaderInt64),
};

#undef FEATURE

static constexpr const char* REQUIRED_EXTENSIONS[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

void link(DeviceFeatures& features) {
  features.core.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  features.core.pNext = &features.vulkan11;
  features.vulkan11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
  features.vulkan11.pNext = &features.vulkan12;
  features.vulkan12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
  features.vulkan12.pNext = &features.vulkan13;
  features.vulkan13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
  features.vulkan13.pNext = &features.vulkan14;
  features.vulkan14.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;
  features.vulkan14.pNext = nullptr;
}

void query_features(VkPhysicalDevice physical_device, DeviceFeatures& features) {
  features = {};
  link(features);
  vkGetPhysicalDeviceFeatures2(physical_device, &features.core);
}

bool select_features(const DeviceFeatures& supported, DeviceFeatures& enabled,
                     const char* device_name) {
  enabled = {};
  link(enabled);

  bool isSupported = true;
  for (const Feature& feature : REQUIRED_FEATURES) {
    if (!feature.read(supported)) {
      std::println(stderr, "[vulkan] {}: missing required feature {}",
                   device_name, feature.name);
      isSupported = false;
      continue;
    }
    feature.get(enabled) = VK_TRUE;
  }

  for (const Feature& feature : OPTIONAL_FEATURES) {
    if (feature.read(supported)) {
      feature.get(enabled) = VK_TRUE;
    }
  }

  return isSupported;
}

std::span<const char* const> required_extensions() {
  return REQUIRED_EXTENSIONS;
}

} // namespace vulkan
