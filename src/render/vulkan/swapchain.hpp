#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace vulkan {

struct Context;

struct SwapchainConfig {
  // Used when the surface leaves the size up to the application (Wayland).
  VkExtent2D extent{};
  // FIFO when on. When off: MAILBOX, then IMMEDIATE, then FIFO.
  bool isVsyncEnabled = true;
};

struct Swapchain {
  VkSwapchainKHR handle = VK_NULL_HANDLE;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkColorSpaceKHR color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
  VkImageUsageFlags usage = 0;
  VkExtent2D extent{};
  std::vector<VkImage> images;
  std::vector<VkImageView> image_views;
};

enum class SwapchainStatus {
  Ok,
  Suboptimal,
  OutOfDate,
  Error,
};

// Creates the swapchain, or recreates it when `swapchain` already holds one.
// Recreation waits for the device to go idle before replacing the old images.
bool create_swapchain(Swapchain& swapchain, const Context& context,
                      const SwapchainConfig& config);
void destroy_swapchain(Swapchain& swapchain, const Context& context);

SwapchainStatus acquire_next_image(const Swapchain& swapchain,
                                   const Context& context,
                                   VkSemaphore signal_semaphore,
                                   uint32_t& image_index);
SwapchainStatus present(const Swapchain& swapchain, const Context& context,
                        VkSemaphore wait_semaphore, uint32_t image_index);

} // namespace vulkan
