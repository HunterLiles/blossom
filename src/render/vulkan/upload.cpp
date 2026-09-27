#include "render/vulkan/upload.hpp"

#include "render/vulkan/context.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <print>

namespace vulkan {

bool create_uploader(Uploader& uploader, const Context& context) {
  VkCommandPoolCreateInfo pool_info{};
  pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  // Graphics rather than a transfer-only queue: mip generation uses blits.
  pool_info.queueFamilyIndex = context.queue_families.graphics;

  VkCommandBufferAllocateInfo allocate_info{};
  allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocate_info.commandBufferCount = 1;

  VkFenceCreateInfo fence_info{};
  fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

  if (vkCreateCommandPool(context.device, &pool_info, nullptr, &uploader.command_pool) !=
      VK_SUCCESS) {
    std::println(stderr, "[upload] vkCreateCommandPool failed");
    return false;
  }
  allocate_info.commandPool = uploader.command_pool;
  if (vkAllocateCommandBuffers(context.device, &allocate_info, &uploader.command_buffer) !=
          VK_SUCCESS ||
      vkCreateFence(context.device, &fence_info, nullptr, &uploader.fence) != VK_SUCCESS) {
    std::println(stderr, "[upload] failed to create command buffer or fence");
    vkDestroyCommandPool(context.device, uploader.command_pool, nullptr);
    uploader = {};
    return false;
  }
  return true;
}

static void free_staging(Uploader& uploader, const Context& context, Allocator& allocator) {
  for (Buffer& buffer : uploader.staging) {
    destroy_buffer(buffer, context, allocator);
  }
  uploader.staging.clear();
}

void destroy_uploader(Uploader& uploader, const Context& context, Allocator& allocator) {
  if (uploader.isRecording) {
    std::println(stderr, "[upload] destroyed with unsubmitted uploads; they are dropped");
  }
  free_staging(uploader, context, allocator);
  vkDestroyFence(context.device, uploader.fence, nullptr);
  // Destroying the pool frees its command buffer.
  vkDestroyCommandPool(context.device, uploader.command_pool, nullptr);
  uploader = {};
}

static void begin_recording(Uploader& uploader) {
  if (uploader.isRecording) {
    return;
  }
  VkCommandBufferBeginInfo begin_info{};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(uploader.command_buffer, &begin_info);
  uploader.isRecording = true;
}

static bool create_staging(Uploader& uploader, const Context& context,
                           Allocator& allocator, const void* data, VkDeviceSize size,
                           VkBuffer& handle) {
  BufferConfig config{};
  config.size = size;
  config.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  config.memory_usage = MemoryUsage::Upload;

  Buffer staging;
  if (!create_buffer(staging, context, allocator, config)) {
    return false;
  }
  std::memcpy(staging.allocation.mapped, data, static_cast<size_t>(size));
  flush(allocator, context, staging.allocation);

  handle = staging.handle;
  uploader.staging.push_back(staging);
  return true;
}

bool upload_buffer(Uploader& uploader, const Context& context, Allocator& allocator,
                   const Buffer& destination, const void* data, VkDeviceSize size) {
  if (size > destination.size) {
    std::println(stderr, "[upload] {} bytes do not fit a {} byte buffer", size,
                 destination.size);
    return false;
  }

  // Integrated GPUs expose GPU-only memory to the CPU, so skip the copy.
  if (destination.allocation.mapped) {
    std::memcpy(destination.allocation.mapped, data, static_cast<size_t>(size));
    flush(allocator, context, destination.allocation);
    return true;
  }

  VkBuffer staging = VK_NULL_HANDLE;
  if (!create_staging(uploader, context, allocator, data, size, staging)) {
    return false;
  }

  begin_recording(uploader);
  VkBufferCopy region{};
  region.size = size;
  vkCmdCopyBuffer(uploader.command_buffer, staging, destination.handle, 1, &region);
  return true;
}

static void image_barrier(VkCommandBuffer command_buffer, const Image& image,
                          uint32_t base_mip, uint32_t mip_count, VkImageLayout old_layout,
                          VkImageLayout new_layout, VkPipelineStageFlags2 src_stage,
                          VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stage,
                          VkAccessFlags2 dst_access) {
  VkImageMemoryBarrier2 barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
  barrier.srcStageMask = src_stage;
  barrier.srcAccessMask = src_access;
  barrier.dstStageMask = dst_stage;
  barrier.dstAccessMask = dst_access;
  barrier.oldLayout = old_layout;
  barrier.newLayout = new_layout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image.handle;
  barrier.subresourceRange.aspectMask = image.aspect;
  barrier.subresourceRange.baseMipLevel = base_mip;
  barrier.subresourceRange.levelCount = mip_count;
  barrier.subresourceRange.layerCount = 1;

  VkDependencyInfo dependency{};
  dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  dependency.imageMemoryBarrierCount = 1;
  dependency.pImageMemoryBarriers = &barrier;
  vkCmdPipelineBarrier2(command_buffer, &dependency);
}

static VkOffset3D mip_size(VkExtent2D extent, uint32_t mip) {
  return {static_cast<int32_t>(std::max(extent.width >> mip, 1u)),
          static_cast<int32_t>(std::max(extent.height >> mip, 1u)), 1};
}

bool upload_image(Uploader& uploader, const Context& context, Allocator& allocator,
                  const Image& destination, const void* pixels, VkDeviceSize size) {
  VkBuffer staging = VK_NULL_HANDLE;
  if (!create_staging(uploader, context, allocator, pixels, size, staging)) {
    return false;
  }

  begin_recording(uploader);
  VkCommandBuffer command_buffer = uploader.command_buffer;

  constexpr VkPipelineStageFlags2 TRANSFER = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
  // Textures may be sampled by any stage later on.
  constexpr VkPipelineStageFlags2 SHADERS = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
  constexpr VkAccessFlags2 SAMPLED = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;

  image_barrier(command_buffer, destination, 0, destination.mip_levels,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, TRANSFER,
                VK_ACCESS_2_TRANSFER_WRITE_BIT);

  VkBufferImageCopy region{};
  region.imageSubresource.aspectMask = destination.aspect;
  region.imageSubresource.layerCount = 1;
  region.imageExtent = {destination.extent.width, destination.extent.height, 1};
  vkCmdCopyBufferToImage(command_buffer, staging, destination.handle,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

  // Each mip is blitted from the one above it. Once a mip has been read it is
  // finished and moves to SHADER_READ_ONLY_OPTIMAL.
  for (uint32_t mip = 1; mip < destination.mip_levels; ++mip) {
    image_barrier(command_buffer, destination, mip - 1, 1,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  TRANSFER, VK_ACCESS_2_TRANSFER_WRITE_BIT, TRANSFER,
                  VK_ACCESS_2_TRANSFER_READ_BIT);

    VkImageBlit blit{};
    blit.srcSubresource = {destination.aspect, mip - 1, 0, 1};
    blit.srcOffsets[1] = mip_size(destination.extent, mip - 1);
    blit.dstSubresource = {destination.aspect, mip, 0, 1};
    blit.dstOffsets[1] = mip_size(destination.extent, mip);
    vkCmdBlitImage(command_buffer, destination.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   destination.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                   VK_FILTER_LINEAR);

    image_barrier(command_buffer, destination, mip - 1, 1,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, TRANSFER,
                  VK_ACCESS_2_TRANSFER_READ_BIT, SHADERS, SAMPLED);
  }

  image_barrier(command_buffer, destination, destination.mip_levels - 1, 1,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                TRANSFER, VK_ACCESS_2_TRANSFER_WRITE_BIT, SHADERS, SAMPLED);
  return true;
}

bool submit_uploads(Uploader& uploader, const Context& context, Allocator& allocator) {
  if (!uploader.isRecording) {
    free_staging(uploader, context, allocator);
    return true;
  }

  // Make buffer copies visible to whatever reads them next (vertex/index
  // fetch, shaders). Image layouts were already handled per image.
  VkMemoryBarrier2 barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
  barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
  barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
  barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
  barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
  VkDependencyInfo dependency{};
  dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  dependency.memoryBarrierCount = 1;
  dependency.pMemoryBarriers = &barrier;
  vkCmdPipelineBarrier2(uploader.command_buffer, &dependency);

  vkEndCommandBuffer(uploader.command_buffer);
  uploader.isRecording = false;

  VkCommandBufferSubmitInfo command_info{};
  command_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
  command_info.commandBuffer = uploader.command_buffer;

  VkSubmitInfo2 submit{};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
  submit.commandBufferInfoCount = 1;
  submit.pCommandBufferInfos = &command_info;

  VkResult result = vkQueueSubmit2(context.graphics_queue, 1, &submit, uploader.fence);
  if (result == VK_SUCCESS) {
    vkWaitForFences(context.device, 1, &uploader.fence, VK_TRUE, UINT64_MAX);
    vkResetFences(context.device, 1, &uploader.fence);
  } else {
    std::println(stderr, "[upload] vkQueueSubmit2 failed: {}", static_cast<int>(result));
  }

  vkResetCommandPool(context.device, uploader.command_pool, 0);
  free_staging(uploader, context, allocator);
  return result == VK_SUCCESS;
}

} // namespace vulkan
