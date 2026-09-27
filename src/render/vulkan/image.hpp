#pragma once

#include "render/vulkan/allocator.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>

namespace vulkan {

struct Context;

struct ImageConfig {
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkExtent2D extent{};
  VkImageUsageFlags usage = 0;
  VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
  uint32_t mip_levels = 1;
  // e.g. VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT to add views in another format.
  VkImageCreateFlags flags = 0;
};

// An optimal-tiling GPU-only 2D image with a view over all of its mip levels.
struct Image {
  VkImage handle = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  // Owned by the allocator.
  Allocation allocation;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkExtent2D extent{};
  uint32_t mip_levels = 1;
  VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
};

bool create_image(Image& image, const Context& context, Allocator& allocator,
                  const ImageConfig& config);
void destroy_image(Image& image, const Context& context, Allocator& allocator);

// An extra view of `image`, e.g. in a different format. Caller destroys it.
VkImageView create_image_view(const Context& context, const Image& image, VkFormat format);

// First depth format usable as an optimal-tiling depth attachment, or
// VK_FORMAT_UNDEFINED.
VkFormat find_depth_format(const Context& context);

// Levels in a full mip chain down to 1x1.
uint32_t full_mip_count(VkExtent2D extent);

// Whether mips can be generated on the GPU with linear-filtered vkCmdBlitImage.
bool supports_linear_blit(const Context& context, VkFormat format);

} // namespace vulkan
