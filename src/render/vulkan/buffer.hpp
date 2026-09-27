#pragma once

#include "render/vulkan/allocator.hpp"

#include <vulkan/vulkan.h>

namespace vulkan {

struct Context;

struct BufferConfig {
  VkDeviceSize size = 0;
  VkBufferUsageFlags usage = 0;
  MemoryUsage memory_usage = MemoryUsage::GpuOnly;
};

struct Buffer {
  VkBuffer handle = VK_NULL_HANDLE;
  VkDeviceSize size = 0;
  // Owned by the allocator. `allocation.mapped` is non-null when the CPU can
  // write the buffer directly.
  Allocation allocation;
};

bool create_buffer(Buffer& buffer, const Context& context, Allocator& allocator,
                   const BufferConfig& config);
void destroy_buffer(Buffer& buffer, const Context& context, Allocator& allocator);

} // namespace vulkan
