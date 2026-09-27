#include "render/raster/renderer.hpp"

#include "render/vulkan/context.hpp"
#include "render/vulkan/shader.hpp"
#include "render/vulkan/upload.hpp"
#include "resources/mesh.hpp"
#include "resources/texture.hpp"
#include "scene/scene.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <print>

namespace raster {

static const std::filesystem::path SHADER_DIR =
    std::filesystem::path(BLOSSOM_SHADER_BINARY_DIR) / "raster";

// Charcoal #222222 to match the GUI. The target is sRGB, so clear values are
// linear: 0x22 decodes to 0.0160.
static constexpr VkClearColorValue CLEAR_COLOR = {{0.0160f, 0.0160f, 0.0160f, 1.0f}};

static constexpr VkShaderStageFlags PUSH_STAGES =
    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

// ---------------------------------------------------------------------------
// Descriptors
// ---------------------------------------------------------------------------

static bool create_descriptors(Renderer& renderer, const vulkan::Context& context) {
  const VkPhysicalDeviceLimits& limits = context.properties.limits;

  // Trilinear filtering with repeat, plus anisotropy when the device has it.
  VkSamplerCreateInfo sampler_info{};
  sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  sampler_info.magFilter = VK_FILTER_LINEAR;
  sampler_info.minFilter = VK_FILTER_LINEAR;
  sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  sampler_info.maxLod = VK_LOD_CLAMP_NONE;
  if (context.features.core.features.samplerAnisotropy) {
    sampler_info.anisotropyEnable = VK_TRUE;
    sampler_info.maxAnisotropy = std::min(16.0f, limits.maxSamplerAnisotropy);
  }
  if (vkCreateSampler(context.device, &sampler_info, nullptr, &renderer.sampler) !=
      VK_SUCCESS) {
    std::println(stderr, "[raster] vkCreateSampler failed");
    return false;
  }

  renderer.texture_capacity = std::min({MAX_TEXTURES, limits.maxPerStageDescriptorSampledImages,
                                        limits.maxDescriptorSetSampledImages});

  VkDescriptorSetLayoutBinding bindings[2]{};
  bindings[0].binding = 0;
  bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
  bindings[0].descriptorCount = 1;
  bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  bindings[0].pImmutableSamplers = &renderer.sampler;
  bindings[1].binding = 1;
  bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  bindings[1].descriptorCount = renderer.texture_capacity;
  bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  // Slots past the scene's textures are never written; partially bound
  // allows that as long as the shader doesn't index them.
  VkDescriptorBindingFlags binding_flags[2] = {0, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT};
  VkDescriptorSetLayoutBindingFlagsCreateInfo flags_info{};
  flags_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
  flags_info.bindingCount = 2;
  flags_info.pBindingFlags = binding_flags;

  VkDescriptorSetLayoutCreateInfo layout_info{};
  layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layout_info.pNext = &flags_info;
  layout_info.bindingCount = 2;
  layout_info.pBindings = bindings;
  if (vkCreateDescriptorSetLayout(context.device, &layout_info, nullptr,
                                  &renderer.set_layout) != VK_SUCCESS) {
    std::println(stderr, "[raster] vkCreateDescriptorSetLayout failed");
    return false;
  }

  VkDescriptorPoolSize pool_sizes[2] = {
      {VK_DESCRIPTOR_TYPE_SAMPLER, 1},
      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, renderer.texture_capacity},
  };
  VkDescriptorPoolCreateInfo pool_info{};
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.maxSets = 1;
  pool_info.poolSizeCount = 2;
  pool_info.pPoolSizes = pool_sizes;
  if (vkCreateDescriptorPool(context.device, &pool_info, nullptr,
                             &renderer.descriptor_pool) != VK_SUCCESS) {
    std::println(stderr, "[raster] vkCreateDescriptorPool failed");
    return false;
  }

  VkDescriptorSetAllocateInfo set_info{};
  set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  set_info.descriptorPool = renderer.descriptor_pool;
  set_info.descriptorSetCount = 1;
  set_info.pSetLayouts = &renderer.set_layout;
  if (vkAllocateDescriptorSets(context.device, &set_info, &renderer.descriptor_set) !=
      VK_SUCCESS) {
    std::println(stderr, "[raster] vkAllocateDescriptorSets failed");
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Pipeline
// ---------------------------------------------------------------------------

static bool create_pipeline(Renderer& renderer, const vulkan::Context& context) {
  VkPushConstantRange push_range{};
  push_range.stageFlags = PUSH_STAGES;
  push_range.size = sizeof(PushConstants);

  VkPipelineLayoutCreateInfo layout_info{};
  layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layout_info.setLayoutCount = 1;
  layout_info.pSetLayouts = &renderer.set_layout;
  layout_info.pushConstantRangeCount = 1;
  layout_info.pPushConstantRanges = &push_range;

  VkResult result = vkCreatePipelineLayout(context.device, &layout_info, nullptr,
                                           &renderer.pipeline_layout);
  if (result != VK_SUCCESS) {
    std::println(stderr, "[raster] vkCreatePipelineLayout failed: {}",
                 static_cast<int>(result));
    return false;
  }

  VkShaderModule vertex_shader =
      vulkan::create_shader_module(context, SHADER_DIR / "mesh.vs.spv");
  VkShaderModule pixel_shader =
      vulkan::create_shader_module(context, SHADER_DIR / "mesh.ps.spv");
  if (vertex_shader == VK_NULL_HANDLE || pixel_shader == VK_NULL_HANDLE) {
    vkDestroyShaderModule(context.device, vertex_shader, nullptr);
    vkDestroyShaderModule(context.device, pixel_shader, nullptr);
    return false;
  }

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vertex_shader;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = pixel_shader;
  stages[1].pName = "main";

  VkVertexInputBindingDescription binding{};
  binding.binding = 0;
  binding.stride = sizeof(resources::Vertex);
  binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  VkVertexInputAttributeDescription attributes[4]{};
  attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT,
                   static_cast<uint32_t>(offsetof(resources::Vertex, position))};
  attributes[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT,
                   static_cast<uint32_t>(offsetof(resources::Vertex, normal))};
  attributes[2] = {2, 0, VK_FORMAT_R32G32B32_SFLOAT,
                   static_cast<uint32_t>(offsetof(resources::Vertex, color))};
  attributes[3] = {3, 0, VK_FORMAT_R32G32_SFLOAT,
                   static_cast<uint32_t>(offsetof(resources::Vertex, uv))};

  VkPipelineVertexInputStateCreateInfo vertex_input{};
  vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertex_input.vertexBindingDescriptionCount = 1;
  vertex_input.pVertexBindingDescriptions = &binding;
  vertex_input.vertexAttributeDescriptionCount = 4;
  vertex_input.pVertexAttributeDescriptions = attributes;

  VkPipelineInputAssemblyStateCreateInfo input_assembly{};
  input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo viewport{};
  viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport.viewportCount = 1;
  viewport.scissorCount = 1;

  // Meshes are clockwise from outside (left-handed). The flipped viewport keeps
  // that winding on screen.
  VkPipelineRasterizationStateCreateInfo rasterization{};
  rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterization.polygonMode = VK_POLYGON_MODE_FILL;
  rasterization.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterization.frontFace = VK_FRONT_FACE_CLOCKWISE;
  rasterization.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisample{};
  multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineDepthStencilStateCreateInfo depth_stencil{};
  depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depth_stencil.depthTestEnable = VK_TRUE;
  depth_stencil.depthWriteEnable = VK_TRUE;
  depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;

  VkPipelineColorBlendAttachmentState blend_attachment{};
  blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendStateCreateInfo color_blend{};
  color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  color_blend.attachmentCount = 1;
  color_blend.pAttachments = &blend_attachment;

  VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic{};
  dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic.dynamicStateCount = 2;
  dynamic.pDynamicStates = dynamic_states;

  VkPipelineRenderingCreateInfo rendering{};
  rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachmentFormats = &SCENE_COLOR_FORMAT;
  rendering.depthAttachmentFormat = renderer.depth_format;

  VkGraphicsPipelineCreateInfo pipeline_info{};
  pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipeline_info.pNext = &rendering;
  pipeline_info.stageCount = 2;
  pipeline_info.pStages = stages;
  pipeline_info.pVertexInputState = &vertex_input;
  pipeline_info.pInputAssemblyState = &input_assembly;
  pipeline_info.pViewportState = &viewport;
  pipeline_info.pRasterizationState = &rasterization;
  pipeline_info.pMultisampleState = &multisample;
  pipeline_info.pDepthStencilState = &depth_stencil;
  pipeline_info.pColorBlendState = &color_blend;
  pipeline_info.pDynamicState = &dynamic;
  pipeline_info.layout = renderer.pipeline_layout;

  result = vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipeline_info,
                                     nullptr, &renderer.pipeline);

  vkDestroyShaderModule(context.device, vertex_shader, nullptr);
  vkDestroyShaderModule(context.device, pixel_shader, nullptr);

  if (result != VK_SUCCESS) {
    std::println(stderr, "[raster] vkCreateGraphicsPipelines failed: {}",
                 static_cast<int>(result));
    renderer.pipeline = VK_NULL_HANDLE;
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Render target
// ---------------------------------------------------------------------------

static void destroy_target(Renderer& renderer, const vulkan::Context& context,
                           vulkan::Allocator& allocator) {
  vkDestroyImageView(context.device, renderer.display_view, nullptr);
  renderer.display_view = VK_NULL_HANDLE;
  vulkan::destroy_image(renderer.color, context, allocator);
  vulkan::destroy_image(renderer.depth, context, allocator);
  renderer.extent = {};
}

static bool create_target(Renderer& renderer, const vulkan::Context& context,
                          vulkan::Allocator& allocator, VkExtent2D extent) {
  vulkan::ImageConfig color_config{};
  color_config.format = SCENE_COLOR_FORMAT;
  color_config.extent = extent;
  color_config.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  color_config.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
  // Allows the UNORM display view alongside the sRGB render view.
  color_config.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;

  vulkan::ImageConfig depth_config{};
  depth_config.format = renderer.depth_format;
  depth_config.extent = extent;
  depth_config.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  depth_config.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;

  if (!vulkan::create_image(renderer.color, context, allocator, color_config) ||
      !vulkan::create_image(renderer.depth, context, allocator, depth_config)) {
    destroy_target(renderer, context, allocator);
    return false;
  }

  renderer.display_view =
      vulkan::create_image_view(context, renderer.color, SCENE_DISPLAY_FORMAT);
  if (renderer.display_view == VK_NULL_HANDLE) {
    destroy_target(renderer, context, allocator);
    return false;
  }

  renderer.extent = extent;
  return true;
}

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

bool create_renderer(Renderer& renderer, const vulkan::Context& context,
                     vulkan::Allocator& allocator, VkExtent2D extent) {
  renderer.depth_format = vulkan::find_depth_format(context);
  if (renderer.depth_format == VK_FORMAT_UNDEFINED) {
    std::println(stderr, "[raster] no supported depth format");
    return false;
  }

  if (!create_target(renderer, context, allocator, extent) ||
      !create_descriptors(renderer, context) || !create_pipeline(renderer, context)) {
    destroy_renderer(renderer, context, allocator);
    return false;
  }
  return true;
}

static void destroy_scene_data(Renderer& renderer, const vulkan::Context& context,
                               vulkan::Allocator& allocator) {
  for (GpuMesh& mesh : renderer.meshes) {
    vulkan::destroy_buffer(mesh.vertices, context, allocator);
    vulkan::destroy_buffer(mesh.indices, context, allocator);
  }
  renderer.meshes.clear();

  for (vulkan::Image& texture : renderer.textures) {
    vulkan::destroy_image(texture, context, allocator);
  }
  renderer.textures.clear();
}

void destroy_renderer(Renderer& renderer, const vulkan::Context& context,
                      vulkan::Allocator& allocator) {
  vkDeviceWaitIdle(context.device);

  destroy_scene_data(renderer, context, allocator);
  destroy_target(renderer, context, allocator);

  vkDestroyPipeline(context.device, renderer.pipeline, nullptr);
  vkDestroyPipelineLayout(context.device, renderer.pipeline_layout, nullptr);
  // Destroying the pool frees its descriptor set.
  vkDestroyDescriptorPool(context.device, renderer.descriptor_pool, nullptr);
  vkDestroyDescriptorSetLayout(context.device, renderer.set_layout, nullptr);
  vkDestroySampler(context.device, renderer.sampler, nullptr);

  renderer = {};
}

bool resize_target(Renderer& renderer, const vulkan::Context& context,
                   vulkan::Allocator& allocator, VkExtent2D extent) {
  vkDeviceWaitIdle(context.device);
  destroy_target(renderer, context, allocator);
  return create_target(renderer, context, allocator, extent);
}

static bool upload_mesh_buffer(vulkan::Buffer& buffer, const vulkan::Context& context,
                               vulkan::Allocator& allocator, vulkan::Uploader& uploader,
                               const void* data, VkDeviceSize size,
                               VkBufferUsageFlags usage) {
  vulkan::BufferConfig config{};
  config.size = size;
  config.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  config.memory_usage = vulkan::MemoryUsage::GpuOnly;
  return vulkan::create_buffer(buffer, context, allocator, config) &&
         vulkan::upload_buffer(uploader, context, allocator, buffer, data, size);
}

static bool upload_texture(vulkan::Image& image, const vulkan::Context& context,
                           vulkan::Allocator& allocator, vulkan::Uploader& uploader,
                           const resources::TextureData& data, bool isMipmapped) {
  VkDeviceSize size = static_cast<VkDeviceSize>(data.width) * data.height * 4;
  if (data.width == 0 || data.height == 0 || data.pixels.size() != size) {
    std::println(stderr, "[raster] texture data is empty or the wrong size");
    return false;
  }

  vulkan::ImageConfig config{};
  config.format = TEXTURE_FORMAT;
  config.extent = {data.width, data.height};
  config.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  config.mip_levels = 1;
  if (isMipmapped) {
    config.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    config.mip_levels = vulkan::full_mip_count(config.extent);
  }

  return vulkan::create_image(image, context, allocator, config) &&
         vulkan::upload_image(uploader, context, allocator, image, data.pixels.data(), size);
}

static void write_texture_descriptors(const Renderer& renderer,
                                      const vulkan::Context& context) {
  std::vector<VkDescriptorImageInfo> image_infos;
  image_infos.reserve(renderer.textures.size());
  for (const vulkan::Image& texture : renderer.textures) {
    image_infos.push_back(
        {VK_NULL_HANDLE, texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
  }

  VkWriteDescriptorSet write{};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.dstSet = renderer.descriptor_set;
  write.dstBinding = 1;
  write.descriptorCount = static_cast<uint32_t>(image_infos.size());
  write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  write.pImageInfo = image_infos.data();
  vkUpdateDescriptorSets(context.device, 1, &write, 0, nullptr);
}

bool upload_scene(Renderer& renderer, const vulkan::Context& context,
                  vulkan::Allocator& allocator, const scene::Scene& scene) {
  // Also required before rewriting descriptors that frames may be using.
  vkDeviceWaitIdle(context.device);
  destroy_scene_data(renderer, context, allocator);

  if (scene.textures.size() + 1 > renderer.texture_capacity) {
    std::println(stderr, "[raster] scene has {} textures, the renderer holds {}",
                 scene.textures.size(), renderer.texture_capacity - 1);
    return false;
  }

  vulkan::Uploader uploader;
  if (!vulkan::create_uploader(uploader, context)) {
    return false;
  }

  bool isMipmapped = vulkan::supports_linear_blit(context, TEXTURE_FORMAT);
  bool isUploaded = true;

  renderer.meshes.resize(scene.meshes.size());
  for (size_t i = 0; i < scene.meshes.size() && isUploaded; ++i) {
    const resources::MeshData& data = scene.meshes[i];
    GpuMesh& mesh = renderer.meshes[i];
    isUploaded = upload_mesh_buffer(mesh.vertices, context, allocator, uploader,
                                    data.vertices.data(),
                                    sizeof(resources::Vertex) * data.vertices.size(),
                                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) &&
                 upload_mesh_buffer(mesh.indices, context, allocator, uploader,
                                    data.indices.data(), sizeof(uint32_t) * data.indices.size(),
                                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    mesh.index_count = static_cast<uint32_t>(data.indices.size());
  }

  renderer.textures.resize(scene.textures.size() + 1);
  const resources::TextureData white = resources::make_solid_texture(255, 255, 255, 255);
  isUploaded = isUploaded && upload_texture(renderer.textures[DEFAULT_TEXTURE], context,
                                            allocator, uploader, white, false);
  for (size_t i = 0; i < scene.textures.size() && isUploaded; ++i) {
    isUploaded = upload_texture(renderer.textures[i + 1], context, allocator, uploader,
                                scene.textures[i], isMipmapped);
  }

  isUploaded = vulkan::submit_uploads(uploader, context, allocator) && isUploaded;
  vulkan::destroy_uploader(uploader, context, allocator);

  if (!isUploaded) {
    destroy_scene_data(renderer, context, allocator);
    return false;
  }
  write_texture_descriptors(renderer, context);
  return true;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

static void transition(VkCommandBuffer command_buffer, VkImage image,
                       VkImageAspectFlags aspect, VkImageLayout old_layout,
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
  barrier.image = image;
  barrier.subresourceRange.aspectMask = aspect;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.layerCount = 1;

  VkDependencyInfo dependency{};
  dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  dependency.imageMemoryBarrierCount = 1;
  dependency.pImageMemoryBarriers = &barrier;
  vkCmdPipelineBarrier2(command_buffer, &dependency);
}

void record_scene(const Renderer& renderer, VkCommandBuffer command_buffer,
                  const scene::Scene& scene, const math::Mat4& view_projection) {
  constexpr VkPipelineStageFlags2 DEPTH_STAGES =
      VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
      VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
  constexpr VkPipelineStageFlags2 COLOR_STAGE =
      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

  // Wait for the previous frame's readers (e.g. the GUI sampling the target)
  // before overwriting it.
  transition(command_buffer, renderer.color.handle, VK_IMAGE_ASPECT_COLOR_BIT,
             VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
             VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | COLOR_STAGE,
             VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, COLOR_STAGE,
             VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
  transition(command_buffer, renderer.depth.handle, VK_IMAGE_ASPECT_DEPTH_BIT,
             VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
             DEPTH_STAGES, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, DEPTH_STAGES,
             VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                 VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

  VkRenderingAttachmentInfo color_attachment{};
  color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  color_attachment.imageView = renderer.color.view;
  color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color_attachment.clearValue.color = CLEAR_COLOR;

  VkRenderingAttachmentInfo depth_attachment{};
  depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  depth_attachment.imageView = renderer.depth.view;
  depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
  depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depth_attachment.clearValue.depthStencil = {1.0f, 0};

  VkRenderingInfo rendering{};
  rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  rendering.renderArea = {{0, 0}, renderer.extent};
  rendering.layerCount = 1;
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachments = &color_attachment;
  rendering.pDepthAttachment = &depth_attachment;

  vkCmdBeginRendering(command_buffer, &rendering);

  // Engine clip space is +Y up and Vulkan's is +Y down. A negative-height
  // viewport (core since Vulkan 1.1) flips it here instead of in the math.
  VkViewport viewport{};
  viewport.y = static_cast<float>(renderer.extent.height);
  viewport.width = static_cast<float>(renderer.extent.width);
  viewport.height = -static_cast<float>(renderer.extent.height);
  viewport.maxDepth = 1.0f;
  VkRect2D scissor = {{0, 0}, renderer.extent};
  vkCmdSetViewport(command_buffer, 0, 1, &viewport);
  vkCmdSetScissor(command_buffer, 0, 1, &scissor);

  vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer.pipeline);
  vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          renderer.pipeline_layout, 0, 1, &renderer.descriptor_set, 0,
                          nullptr);

  for (size_t i = 0; i < scene.object_meshes.size(); ++i) {
    const GpuMesh& mesh = renderer.meshes[scene.object_meshes[i]];
    const math::Mat4& model = scene.object_transforms[i];

    uint32_t texture = scene.object_textures[i];
    PushConstants push{view_projection * model, model,
                       texture == scene::NO_TEXTURE ? DEFAULT_TEXTURE : texture + 1};
    vkCmdPushConstants(command_buffer, renderer.pipeline_layout, PUSH_STAGES, 0,
                       sizeof(PushConstants), &push);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command_buffer, 0, 1, &mesh.vertices.handle, &offset);
    vkCmdBindIndexBuffer(command_buffer, mesh.indices.handle, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(command_buffer, mesh.index_count, 1, 0, 0, 0);
  }

  vkCmdEndRendering(command_buffer);

  transition(command_buffer, renderer.color.handle, VK_IMAGE_ASPECT_COLOR_BIT,
             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, COLOR_STAGE,
             VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
             VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

} // namespace raster
