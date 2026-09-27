#pragma once

#include "render/vulkan/swapchain.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace vulkan {

struct Context;

inline constexpr uint32_t FRAMES_IN_FLIGHT = 2;

struct FrameSync {
  VkCommandBuffer command_buffer = VK_NULL_HANDLE;
  VkSemaphore image_available = VK_NULL_HANDLE;
  VkFence in_flight = VK_NULL_HANDLE;
};

// Per-frame command buffers and synchronization for drawing to the swapchain.
// Systems record into the frame's command buffer between begin_frame and
// end_frame; the frame loop owns acquiring, submitting, and presenting.
struct FrameLoop {
  VkCommandPool command_pool = VK_NULL_HANDLE;
  FrameSync frames[FRAMES_IN_FLIGHT]{};
  uint32_t frame_index = 0;
  // One per swapchain image, so presentation never waits on a reused semaphore.
  std::vector<VkSemaphore> render_finished;
};

// Valid between a successful begin_frame and its end_frame.
struct Frame {
  VkCommandBuffer command_buffer = VK_NULL_HANDLE;
  uint32_t image_index = 0;
  SwapchainStatus acquire_status = SwapchainStatus::Ok;
};

bool create_frame_loop(FrameLoop& loop, const Context& context, const Swapchain& swapchain);
void destroy_frame_loop(FrameLoop& loop, const Context& context);

// Call after the swapchain is recreated (the image count can change).
bool resize_frame_loop(FrameLoop& loop, const Context& context, const Swapchain& swapchain);

// Waits for this frame slot, acquires a swapchain image, and begins the
// command buffer with that image in COLOR_ATTACHMENT_OPTIMAL. Returns
// OutOfDate or Error without beginning anything; recreate and try again.
SwapchainStatus begin_frame(FrameLoop& loop, const Context& context,
                            const Swapchain& swapchain, Frame& frame);

// Moves the image to PRESENT_SRC, submits, and presents. Returns the worse of
// the acquire and present results so the caller knows to recreate.
SwapchainStatus end_frame(FrameLoop& loop, const Context& context,
                          const Swapchain& swapchain, const Frame& frame);

} // namespace vulkan
