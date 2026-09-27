#pragma once

#include "render/vulkan/allocator.hpp"
#include "render/vulkan/buffer.hpp"
#include "render/vulkan/image.hpp"

#include <vulkan/vulkan.h>

#include <vector>

namespace vulkan {

struct Context;

// Copies CPU data into GPU-only buffers and images through staging buffers.
// Uploads are recorded into one command buffer and run together by
// submit_uploads(), which waits for the GPU and then frees the staging memory.
// Meant for load time, not per-frame streaming.
struct Uploader {
  VkCommandPool command_pool = VK_NULL_HANDLE;
  VkCommandBuffer command_buffer = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  // Kept alive until the recorded copies have run.
  std::vector<Buffer> staging;
  bool isRecording = false;
};

bool create_uploader(Uploader& uploader, const Context& context);
void destroy_uploader(Uploader& uploader, const Context& context, Allocator& allocator);

// `destination` needs VK_BUFFER_USAGE_TRANSFER_DST_BIT. When its memory is
// host visible (integrated GPUs), the data is written directly instead.
bool upload_buffer(Uploader& uploader, const Context& context, Allocator& allocator,
                   const Buffer& destination, const void* data, VkDeviceSize size);

// Fills mip 0 from tightly packed `pixels` and generates the remaining mips
// with linear blits. `destination` needs TRANSFER_DST, plus TRANSFER_SRC when
// it has more than one mip, and supports_linear_blit() for its format.
// Every mip ends in SHADER_READ_ONLY_OPTIMAL.
bool upload_image(Uploader& uploader, const Context& context, Allocator& allocator,
                  const Image& destination, const void* pixels, VkDeviceSize size);

// Runs everything recorded since the last submit and waits for it to finish.
bool submit_uploads(Uploader& uploader, const Context& context, Allocator& allocator);

} // namespace vulkan
