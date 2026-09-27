#include "render/vulkan/frame.hpp"

#include "render/vulkan/context.hpp"

#include <cstdio>
#include <print>

namespace vulkan {

static void destroy_render_semaphores(FrameLoop& loop, const Context& context) {
  for (VkSemaphore semaphore : loop.render_finished) {
    vkDestroySemaphore(context.device, semaphore, nullptr);
  }
  loop.render_finished.clear();
}

static bool create_render_semaphores(FrameLoop& loop, const Context& context,
                                     const Swapchain& swapchain) {
  VkSemaphoreCreateInfo semaphore_info{};
  semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  loop.render_finished.resize(swapchain.images.size(), VK_NULL_HANDLE);
  for (VkSemaphore& semaphore : loop.render_finished) {
    if (vkCreateSemaphore(context.device, &semaphore_info, nullptr, &semaphore) !=
        VK_SUCCESS) {
      std::println(stderr, "[frame] failed to create render semaphore");
      return false;
    }
  }
  return true;
}

bool create_frame_loop(FrameLoop& loop, const Context& context, const Swapchain& swapchain) {
  VkCommandPoolCreateInfo pool_info{};
  pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pool_info.queueFamilyIndex = context.queue_families.graphics;

  if (vkCreateCommandPool(context.device, &pool_info, nullptr, &loop.command_pool) !=
      VK_SUCCESS) {
    std::println(stderr, "[frame] vkCreateCommandPool failed");
    return false;
  }

  VkCommandBufferAllocateInfo allocate_info{};
  allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocate_info.commandPool = loop.command_pool;
  allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocate_info.commandBufferCount = 1;

  VkSemaphoreCreateInfo semaphore_info{};
  semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  VkFenceCreateInfo fence_info{};
  fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  for (FrameSync& sync : loop.frames) {
    if (vkAllocateCommandBuffers(context.device, &allocate_info, &sync.command_buffer) !=
            VK_SUCCESS ||
        vkCreateSemaphore(context.device, &semaphore_info, nullptr,
                          &sync.image_available) != VK_SUCCESS ||
        vkCreateFence(context.device, &fence_info, nullptr, &sync.in_flight) != VK_SUCCESS) {
      std::println(stderr, "[frame] failed to create frame resources");
      destroy_frame_loop(loop, context);
      return false;
    }
  }

  if (!create_render_semaphores(loop, context, swapchain)) {
    destroy_frame_loop(loop, context);
    return false;
  }
  return true;
}

void destroy_frame_loop(FrameLoop& loop, const Context& context) {
  vkDeviceWaitIdle(context.device);
  destroy_render_semaphores(loop, context);
  for (FrameSync& sync : loop.frames) {
    vkDestroyFence(context.device, sync.in_flight, nullptr);
    vkDestroySemaphore(context.device, sync.image_available, nullptr);
  }
  // Destroying the pool frees its command buffers.
  vkDestroyCommandPool(context.device, loop.command_pool, nullptr);
  loop = {};
}

bool resize_frame_loop(FrameLoop& loop, const Context& context, const Swapchain& swapchain) {
  vkDeviceWaitIdle(context.device);
  destroy_render_semaphores(loop, context);
  return create_render_semaphores(loop, context, swapchain);
}

static void transition_swapchain_image(VkCommandBuffer command_buffer, VkImage image,
                                       VkImageLayout old_layout, VkImageLayout new_layout,
                                       VkAccessFlags2 src_access, VkAccessFlags2 dst_access) {
  VkImageMemoryBarrier2 barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
  barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
  barrier.srcAccessMask = src_access;
  barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
  barrier.dstAccessMask = dst_access;
  barrier.oldLayout = old_layout;
  barrier.newLayout = new_layout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.layerCount = 1;

  VkDependencyInfo dependency{};
  dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  dependency.imageMemoryBarrierCount = 1;
  dependency.pImageMemoryBarriers = &barrier;
  vkCmdPipelineBarrier2(command_buffer, &dependency);
}

SwapchainStatus begin_frame(FrameLoop& loop, const Context& context,
                            const Swapchain& swapchain, Frame& frame) {
  FrameSync& sync = loop.frames[loop.frame_index];
  vkWaitForFences(context.device, 1, &sync.in_flight, VK_TRUE, UINT64_MAX);

  frame = {};
  frame.acquire_status =
      acquire_next_image(swapchain, context, sync.image_available, frame.image_index);
  if (frame.acquire_status == SwapchainStatus::OutOfDate ||
      frame.acquire_status == SwapchainStatus::Error) {
    return frame.acquire_status;
  }

  // Only reset once work is guaranteed to be submitted, or the next wait hangs.
  vkResetFences(context.device, 1, &sync.in_flight);
  vkResetCommandBuffer(sync.command_buffer, 0);

  VkCommandBufferBeginInfo begin_info{};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(sync.command_buffer, &begin_info);

  transition_swapchain_image(sync.command_buffer, swapchain.images[frame.image_index],
                             VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_2_NONE,
                             VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

  frame.command_buffer = sync.command_buffer;
  return frame.acquire_status;
}

SwapchainStatus end_frame(FrameLoop& loop, const Context& context,
                          const Swapchain& swapchain, const Frame& frame) {
  FrameSync& sync = loop.frames[loop.frame_index];

  transition_swapchain_image(frame.command_buffer, swapchain.images[frame.image_index],
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                             VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                             VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_NONE);
  vkEndCommandBuffer(frame.command_buffer);

  VkSemaphore render_finished = loop.render_finished[frame.image_index];

  VkSemaphoreSubmitInfo wait_info{};
  wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
  wait_info.semaphore = sync.image_available;
  wait_info.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

  VkSemaphoreSubmitInfo signal_info{};
  signal_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
  signal_info.semaphore = render_finished;
  signal_info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

  VkCommandBufferSubmitInfo command_info{};
  command_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
  command_info.commandBuffer = frame.command_buffer;

  VkSubmitInfo2 submit{};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
  submit.waitSemaphoreInfoCount = 1;
  submit.pWaitSemaphoreInfos = &wait_info;
  submit.commandBufferInfoCount = 1;
  submit.pCommandBufferInfos = &command_info;
  submit.signalSemaphoreInfoCount = 1;
  submit.pSignalSemaphoreInfos = &signal_info;

  VkResult result = vkQueueSubmit2(context.graphics_queue, 1, &submit, sync.in_flight);
  if (result != VK_SUCCESS) {
    std::println(stderr, "[frame] vkQueueSubmit2 failed: {}", static_cast<int>(result));
    return SwapchainStatus::Error;
  }

  loop.frame_index = (loop.frame_index + 1) % FRAMES_IN_FLIGHT;

  SwapchainStatus present_status =
      present(swapchain, context, render_finished, frame.image_index);
  if (present_status != SwapchainStatus::Ok) {
    return present_status;
  }
  return frame.acquire_status;
}

} // namespace vulkan
