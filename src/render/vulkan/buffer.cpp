#include "render/vulkan/buffer.hpp"

#include "render/vulkan/context.hpp"

#include <cstdio>
#include <print>

namespace vulkan {

bool create_buffer(Buffer& buffer, const Context& context, Allocator& allocator,
                   const BufferConfig& config) {
  VkBufferCreateInfo buffer_info{};
  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size = config.size;
  buffer_info.usage = config.usage;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VkResult result = vkCreateBuffer(context.device, &buffer_info, nullptr, &buffer.handle);
  if (result != VK_SUCCESS) {
    std::println(stderr, "[vulkan] vkCreateBuffer failed: {}", static_cast<int>(result));
    buffer = {};
    return false;
  }

  if (!allocate_buffer(allocator, context, buffer.handle, config.memory_usage,
                       buffer.allocation)) {
    destroy_buffer(buffer, context, allocator);
    return false;
  }

  buffer.size = config.size;
  return true;
}

void destroy_buffer(Buffer& buffer, const Context& context, Allocator& allocator) {
  if (buffer.handle != VK_NULL_HANDLE) {
    vkDestroyBuffer(context.device, buffer.handle, nullptr);
  }
  release(allocator, context, buffer.allocation);
  buffer = {};
}

} // namespace vulkan
