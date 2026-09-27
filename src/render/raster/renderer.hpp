#pragma once

#include "math/math.hpp"
#include "render/vulkan/allocator.hpp"
#include "render/vulkan/buffer.hpp"
#include "render/vulkan/image.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace vulkan {
struct Context;
}

namespace scene {
struct Scene;
}

namespace raster {

// Size of the bindless texture array (clamped to the device limits).
inline constexpr uint32_t MAX_TEXTURES = 1024;
// Slot 0 of the texture array is a 1x1 white texture for untextured objects.
inline constexpr uint32_t DEFAULT_TEXTURE = 0;
// Color textures are sRGB, so sampling returns linear values.
inline constexpr VkFormat TEXTURE_FORMAT = VK_FORMAT_R8G8B8A8_SRGB;

// The scene is rendered into an sRGB target (lighting stays linear), and
// displayed through a UNORM view of the same memory so a UNORM presenter
// (the GUI and swapchain) shows the stored bytes unchanged.
inline constexpr VkFormat SCENE_COLOR_FORMAT = VK_FORMAT_R8G8B8A8_SRGB;
inline constexpr VkFormat SCENE_DISPLAY_FORMAT = VK_FORMAT_R8G8B8A8_UNORM;

// Matches PushConstants in shaders/raster/mesh.hlsli.
struct PushConstants {
  math::Mat4 mvp;
  math::Mat4 model;
  uint32_t texture_index = DEFAULT_TEXTURE;
};

// Vulkan 1.4 guarantees at least 256 bytes of push constants.
static_assert(sizeof(PushConstants) <= 256);

// GPU copy of one scene mesh. The scene keeps ownership of the source data.
struct GpuMesh {
  vulkan::Buffer vertices;
  vulkan::Buffer indices;
  uint32_t index_count = 0;
};

// Draws a scene into a color image it owns. It knows nothing about the
// swapchain or presentation; whoever displays the result reads `display_view`.
struct Renderer {
  VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;

  // Set 0: binding 0 is the shared sampler (immutable), binding 1 the
  // bindless texture array indexed by PushConstants::texture_index.
  VkSampler sampler = VK_NULL_HANDLE;
  VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
  VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
  VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
  uint32_t texture_capacity = 0;

  // Render target. After record_scene, `color` is in SHADER_READ_ONLY_OPTIMAL.
  VkExtent2D extent{};
  vulkan::Image color;
  VkImageView display_view = VK_NULL_HANDLE;
  vulkan::Image depth;
  VkFormat depth_format = VK_FORMAT_UNDEFINED;

  // GPU copies of scene data. The scene keeps ownership of the source data.
  std::vector<GpuMesh> meshes;
  // Index 0 is the default texture; scene texture i is at i + 1.
  std::vector<vulkan::Image> textures;
};

bool create_renderer(Renderer& renderer, const vulkan::Context& context,
                     vulkan::Allocator& allocator, VkExtent2D extent);
void destroy_renderer(Renderer& renderer, const vulkan::Context& context,
                      vulkan::Allocator& allocator);

// Recreates the render target at a new size. Waits for the GPU first, so
// anything that referenced the old `display_view` must be updated after.
bool resize_target(Renderer& renderer, const vulkan::Context& context,
                   vulkan::Allocator& allocator, VkExtent2D extent);

// Replaces the renderer's GPU meshes and textures with copies of the scene's.
// Uploads go through staging into GPU-only memory and finish before it returns.
bool upload_scene(Renderer& renderer, const vulkan::Context& context,
                  vulkan::Allocator& allocator, const scene::Scene& scene);

// Records the scene into the render target. The target ends in
// SHADER_READ_ONLY_OPTIMAL, ready to be sampled by fragment shaders.
void record_scene(const Renderer& renderer, VkCommandBuffer command_buffer,
                  const scene::Scene& scene, const math::Mat4& view_projection);

} // namespace raster
