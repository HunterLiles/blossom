#include "render/vulkan/swapchain.hpp"

#include "render/vulkan/context.hpp"

#include <algorithm>
#include <cstdio>
#include <print>

namespace vulkan {

// Every swapchain image must be renderable. Transfer destination is added when
// supported so the CPU rasterizer and compute renderers can copy/blit into it.
static constexpr VkImageUsageFlags REQUIRED_USAGE = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
static constexpr VkImageUsageFlags OPTIONAL_USAGE = VK_IMAGE_USAGE_TRANSFER_DST_BIT;

static VkSurfaceFormatKHR choose_format(VkPhysicalDevice physical_device,
                                        VkSurfaceKHR surface) {
  uint32_t count = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &count, nullptr);
  std::vector<VkSurfaceFormatKHR> formats(count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &count, formats.data());

  // UNORM, not sRGB: the swapchain shows the GUI, and ImGui's colors are
  // already sRGB-encoded. Renderers draw into sRGB targets of their own and
  // are displayed through UNORM views, so their output stays correct too.
  for (VkFormat preferred : {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM}) {
    for (const VkSurfaceFormatKHR& format : formats) {
      if (format.format == preferred &&
          format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        return format;
      }
    }
  }
  return formats.empty() ? VkSurfaceFormatKHR{} : formats[0];
}

static VkPresentModeKHR choose_present_mode(VkPhysicalDevice physical_device,
                                            VkSurfaceKHR surface,
                                            bool isVsyncEnabled) {
  if (isVsyncEnabled) {
    return VK_PRESENT_MODE_FIFO_KHR;
  }

  uint32_t count = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &count, nullptr);
  std::vector<VkPresentModeKHR> modes(count);
  vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &count, modes.data());

  for (VkPresentModeKHR preferred :
       {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR}) {
    if (std::find(modes.begin(), modes.end(), preferred) != modes.end()) {
      return preferred;
    }
  }
  // FIFO is the only mode every implementation must support.
  return VK_PRESENT_MODE_FIFO_KHR;
}

static VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR& capabilities,
                                VkExtent2D requested) {
  if (capabilities.currentExtent.width != UINT32_MAX) {
    return capabilities.currentExtent;
  }
  return {
      std::clamp(requested.width, capabilities.minImageExtent.width,
                 capabilities.maxImageExtent.width),
      std::clamp(requested.height, capabilities.minImageExtent.height,
                 capabilities.maxImageExtent.height),
  };
}

static VkCompositeAlphaFlagBitsKHR
choose_composite_alpha(VkCompositeAlphaFlagsKHR supported) {
  for (VkCompositeAlphaFlagBitsKHR alpha :
       {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR}) {
    if (supported & alpha) {
      return alpha;
    }
  }
  return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

static void destroy_image_views(Swapchain& swapchain, const Context& context) {
  for (VkImageView view : swapchain.image_views) {
    vkDestroyImageView(context.device, view, nullptr);
  }
  swapchain.image_views.clear();
  swapchain.images.clear();
}

static bool create_image_views(Swapchain& swapchain, const Context& context) {
  uint32_t count = 0;
  vkGetSwapchainImagesKHR(context.device, swapchain.handle, &count, nullptr);
  swapchain.images.resize(count);
  vkGetSwapchainImagesKHR(context.device, swapchain.handle, &count,
                          swapchain.images.data());

  swapchain.image_views.reserve(count);
  for (VkImage image : swapchain.images) {
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = swapchain.format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;

    VkImageView view = VK_NULL_HANDLE;
    VkResult result = vkCreateImageView(context.device, &view_info, nullptr, &view);
    if (result != VK_SUCCESS) {
      std::println(stderr, "[vulkan] vkCreateImageView failed: {}",
                   static_cast<int>(result));
      return false;
    }
    swapchain.image_views.push_back(view);
  }
  return true;
}

bool create_swapchain(Swapchain& swapchain, const Context& context,
                      const SwapchainConfig& config) {
  VkSurfaceCapabilitiesKHR capabilities{};
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context.physical_device, context.surface,
                                            &capabilities);

  if ((capabilities.supportedUsageFlags & REQUIRED_USAGE) != REQUIRED_USAGE) {
    std::println(stderr, "[vulkan] surface does not support color attachment images");
    return false;
  }

  VkSurfaceFormatKHR surface_format =
      choose_format(context.physical_device, context.surface);
  if (surface_format.format == VK_FORMAT_UNDEFINED) {
    std::println(stderr, "[vulkan] surface reports no formats");
    return false;
  }

  VkExtent2D extent = choose_extent(capabilities, config.extent);
  if (extent.width == 0 || extent.height == 0) {
    std::println(stderr, "[vulkan] cannot create a {}x{} swapchain", extent.width,
                 extent.height);
    return false;
  }

  uint32_t image_count = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount > 0) {
    image_count = std::min(image_count, capabilities.maxImageCount);
  }

  VkSwapchainKHR old_swapchain = swapchain.handle;
  if (old_swapchain != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(context.device);
  }

  VkSwapchainCreateInfoKHR create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  create_info.surface = context.surface;
  create_info.minImageCount = image_count;
  create_info.imageFormat = surface_format.format;
  create_info.imageColorSpace = surface_format.colorSpace;
  create_info.imageExtent = extent;
  create_info.imageArrayLayers = 1;
  create_info.imageUsage =
      REQUIRED_USAGE | (capabilities.supportedUsageFlags & OPTIONAL_USAGE);
  create_info.preTransform = capabilities.currentTransform;
  create_info.compositeAlpha =
      choose_composite_alpha(capabilities.supportedCompositeAlpha);
  create_info.presentMode = choose_present_mode(context.physical_device,
                                                context.surface, config.isVsyncEnabled);
  create_info.clipped = VK_TRUE;
  create_info.oldSwapchain = old_swapchain;

  const QueueFamilies& families = context.queue_families;
  uint32_t shared_families[] = {families.graphics, families.present};
  if (families.graphics != families.present) {
    create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    create_info.queueFamilyIndexCount = 2;
    create_info.pQueueFamilyIndices = shared_families;
  } else {
    create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  }

  VkSwapchainKHR new_swapchain = VK_NULL_HANDLE;
  VkResult result =
      vkCreateSwapchainKHR(context.device, &create_info, nullptr, &new_swapchain);

  // The old swapchain is retired either way once it was passed as oldSwapchain.
  if (old_swapchain != VK_NULL_HANDLE) {
    destroy_image_views(swapchain, context);
    vkDestroySwapchainKHR(context.device, old_swapchain, nullptr);
    swapchain.handle = VK_NULL_HANDLE;
  }

  if (result != VK_SUCCESS) {
    std::println(stderr, "[vulkan] vkCreateSwapchainKHR failed: {}",
                 static_cast<int>(result));
    return false;
  }

  swapchain.handle = new_swapchain;
  swapchain.format = surface_format.format;
  swapchain.color_space = surface_format.colorSpace;
  swapchain.present_mode = create_info.presentMode;
  swapchain.usage = create_info.imageUsage;
  swapchain.extent = extent;

  if (!create_image_views(swapchain, context)) {
    destroy_swapchain(swapchain, context);
    return false;
  }
  return true;
}

void destroy_swapchain(Swapchain& swapchain, const Context& context) {
  destroy_image_views(swapchain, context);
  if (swapchain.handle != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(context.device, swapchain.handle, nullptr);
  }
  swapchain = {};
}

static SwapchainStatus to_status(VkResult result, const char* call) {
  switch (result) {
  case VK_SUCCESS:
    return SwapchainStatus::Ok;
  case VK_SUBOPTIMAL_KHR:
    return SwapchainStatus::Suboptimal;
  case VK_ERROR_OUT_OF_DATE_KHR:
    return SwapchainStatus::OutOfDate;
  default:
    std::println(stderr, "[vulkan] {} failed: {}", call, static_cast<int>(result));
    return SwapchainStatus::Error;
  }
}

SwapchainStatus acquire_next_image(const Swapchain& swapchain,
                                   const Context& context,
                                   VkSemaphore signal_semaphore,
                                   uint32_t& image_index) {
  VkResult result = vkAcquireNextImageKHR(context.device, swapchain.handle, UINT64_MAX,
                                          signal_semaphore, VK_NULL_HANDLE, &image_index);
  return to_status(result, "vkAcquireNextImageKHR");
}

SwapchainStatus present(const Swapchain& swapchain, const Context& context,
                        VkSemaphore wait_semaphore, uint32_t image_index) {
  VkPresentInfoKHR present_info{};
  present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  present_info.waitSemaphoreCount = wait_semaphore != VK_NULL_HANDLE ? 1u : 0u;
  present_info.pWaitSemaphores = &wait_semaphore;
  present_info.swapchainCount = 1;
  present_info.pSwapchains = &swapchain.handle;
  present_info.pImageIndices = &image_index;

  VkResult result = vkQueuePresentKHR(context.present_queue, &present_info);
  return to_status(result, "vkQueuePresentKHR");
}

} // namespace vulkan
