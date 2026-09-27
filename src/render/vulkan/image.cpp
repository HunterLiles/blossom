#include "render/vulkan/image.hpp"

#include "render/vulkan/context.hpp"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <print>

namespace vulkan {

bool create_image(Image& image, const Context& context, Allocator& allocator,
                  const ImageConfig& config) {
  VkImageCreateInfo image_info{};
  image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_info.flags = config.flags;
  image_info.imageType = VK_IMAGE_TYPE_2D;
  image_info.format = config.format;
  image_info.extent = {config.extent.width, config.extent.height, 1};
  image_info.mipLevels = config.mip_levels;
  image_info.arrayLayers = 1;
  image_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_info.usage = config.usage;
  image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VkResult result = vkCreateImage(context.device, &image_info, nullptr, &image.handle);
  if (result != VK_SUCCESS) {
    std::println(stderr, "[vulkan] vkCreateImage failed: {}", static_cast<int>(result));
    image = {};
    return false;
  }

  if (!allocate_image(allocator, context, image.handle, MemoryUsage::GpuOnly,
                      ResourceKind::Optimal, image.allocation)) {
    destroy_image(image, context, allocator);
    return false;
  }

  VkImageViewCreateInfo view_info{};
  view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view_info.image = image.handle;
  view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view_info.format = config.format;
  view_info.subresourceRange.aspectMask = config.aspect;
  view_info.subresourceRange.levelCount = config.mip_levels;
  view_info.subresourceRange.layerCount = 1;

  result = vkCreateImageView(context.device, &view_info, nullptr, &image.view);
  if (result != VK_SUCCESS) {
    std::println(stderr, "[vulkan] vkCreateImageView failed: {}", static_cast<int>(result));
    destroy_image(image, context, allocator);
    return false;
  }

  image.format = config.format;
  image.extent = config.extent;
  image.mip_levels = config.mip_levels;
  image.aspect = config.aspect;
  return true;
}

void destroy_image(Image& image, const Context& context, Allocator& allocator) {
  if (image.view != VK_NULL_HANDLE) {
    vkDestroyImageView(context.device, image.view, nullptr);
  }
  if (image.handle != VK_NULL_HANDLE) {
    vkDestroyImage(context.device, image.handle, nullptr);
  }
  release(allocator, context, image.allocation);
  image = {};
}

VkImageView create_image_view(const Context& context, const Image& image, VkFormat format) {
  VkImageViewCreateInfo view_info{};
  view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view_info.image = image.handle;
  view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view_info.format = format;
  view_info.subresourceRange.aspectMask = image.aspect;
  view_info.subresourceRange.levelCount = image.mip_levels;
  view_info.subresourceRange.layerCount = 1;

  VkImageView view = VK_NULL_HANDLE;
  VkResult result = vkCreateImageView(context.device, &view_info, nullptr, &view);
  if (result != VK_SUCCESS) {
    std::println(stderr, "[vulkan] vkCreateImageView failed: {}", static_cast<int>(result));
    return VK_NULL_HANDLE;
  }
  return view;
}

VkFormat find_depth_format(const Context& context) {
  for (VkFormat format : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT,
                          VK_FORMAT_D24_UNORM_S8_UINT}) {
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(context.physical_device, format, &properties);
    if (properties.optimalTilingFeatures &
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
      return format;
    }
  }
  return VK_FORMAT_UNDEFINED;
}

uint32_t full_mip_count(VkExtent2D extent) {
  return static_cast<uint32_t>(std::bit_width(std::max({extent.width, extent.height, 1u})));
}

bool supports_linear_blit(const Context& context, VkFormat format) {
  constexpr VkFormatFeatureFlags REQUIRED = VK_FORMAT_FEATURE_BLIT_SRC_BIT |
                                            VK_FORMAT_FEATURE_BLIT_DST_BIT |
                                            VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
  VkFormatProperties properties{};
  vkGetPhysicalDeviceFormatProperties(context.physical_device, format, &properties);
  return (properties.optimalTilingFeatures & REQUIRED) == REQUIRED;
}

} // namespace vulkan
