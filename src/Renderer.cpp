#include "Renderer.hpp"

#include "Scene.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr uint32_t windowWidth = 1280;
constexpr uint32_t windowHeight = 720;
constexpr uint32_t cubeVertexCount = 36;
constexpr uint32_t gridVertexCount = 90;

struct SwapchainSupport {
  VkSurfaceCapabilitiesKHR capabilities{};
  std::vector<VkSurfaceFormatKHR> formats;
  std::vector<VkPresentModeKHR> presentModes;
};

struct UniformData {
  // Row-vector transforms, matching GLSL's std140, row_major block.
  math::Mat4 mvp{};
  math::Mat4 viewProjection{};
  float globalLight = 1.0f;
  float padding[3]{};
};

static_assert(sizeof(UniformData) == 144);
static_assert(offsetof(UniformData, viewProjection) == 64);
static_assert(offsetof(UniformData, globalLight) == 128);

void check(VkResult result, std::string_view action) {
  if (result != VK_SUCCESS) {
    throw std::runtime_error(std::string(action) + " failed (VkResult " +
                             std::to_string(result) + ")");
  }
}

void onFramebufferResize(GLFWwindow *window, int, int) {
  static_cast<Renderer *>(glfwGetWindowUserPointer(window))->framebufferResized = true;
}

std::vector<uint32_t> readShader(const std::string &path) {
  std::ifstream file(path, std::ios::ate | std::ios::binary);
  if (!file) {
    throw std::runtime_error("Cannot open shader: " + path);
  }
  const std::streamoff size = file.tellg();
  if (size <= 0 || size % sizeof(uint32_t) != 0) {
    throw std::runtime_error("Invalid SPIR-V size: " + path);
  }
  std::vector<uint32_t> words(static_cast<size_t>(size) / sizeof(uint32_t));
  file.seekg(0);
  if (!file.read(reinterpret_cast<char *>(words.data()), size)) {
    throw std::runtime_error("Cannot read shader: " + path);
  }
  return words;
}

VkShaderModule createShaderModule(const Renderer &engine, const std::string &path) {
  const std::vector<uint32_t> code = readShader(path);
  VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  info.codeSize = code.size() * sizeof(uint32_t);
  info.pCode = code.data();
  VkShaderModule module = VK_NULL_HANDLE;
  check(vkCreateShaderModule(engine.device, &info, nullptr, &module),
        "create shader module");
  return module;
}

SwapchainSupport getSwapchainSupport(VkPhysicalDevice device, VkSurfaceKHR surface) {
  SwapchainSupport support{};
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &support.capabilities);
  uint32_t count = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, nullptr);
  support.formats.resize(count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, support.formats.data());
  vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &count, nullptr);
  support.presentModes.resize(count);
  vkGetPhysicalDeviceSurfacePresentModesKHR(
      device, surface, &count, support.presentModes.data());
  return support;
}

QueueFamilies findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) {
  QueueFamilies result{};
  uint32_t count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
  std::vector<VkQueueFamilyProperties> properties(count);
  vkGetPhysicalDeviceQueueFamilyProperties(device, &count, properties.data());
  for (uint32_t i = 0; i < count; ++i) {
    if (properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      result.graphics = i;
    }
    VkBool32 present = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present);
    if (present) {
      result.present = i;
    }
    if (result.graphics != UINT32_MAX && result.present != UINT32_MAX) {
      break;
    }
  }
  return result;
}

bool supportsDevice(VkPhysicalDevice device,
                    VkSurfaceKHR surface,
                    QueueFamilies &queues) {
  queues = findQueueFamilies(device, surface);
  if (queues.graphics == UINT32_MAX || queues.present == UINT32_MAX) {
    return false;
  }
  uint32_t count = 0;
  vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> extensions(count);
  vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());
  const bool hasSwapchain =
      std::any_of(extensions.begin(), extensions.end(), [](const auto &extension) {
        return std::string_view(extension.extensionName) ==
               VK_KHR_SWAPCHAIN_EXTENSION_NAME;
      });
  const SwapchainSupport support = getSwapchainSupport(device, surface);
  return hasSwapchain && !support.formats.empty() && !support.presentModes.empty();
}

uint32_t findMemoryType(const Renderer &engine,
                        uint32_t typeMask,
                        VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties memory{};
  vkGetPhysicalDeviceMemoryProperties(engine.physicalDevice, &memory);
  for (uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
    if ((typeMask & (1u << i)) &&
        (memory.memoryTypes[i].propertyFlags & properties) == properties) {
      return i;
    }
  }
  throw std::runtime_error("No compatible Vulkan memory type");
}

void createBuffer(const Renderer &engine,
                  VkDeviceSize size,
                  VkBufferUsageFlags usage,
                  VkMemoryPropertyFlags properties,
                  VkBuffer &buffer,
                  VkDeviceMemory &memory) {
  VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bufferInfo.size = size;
  bufferInfo.usage = usage;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  check(vkCreateBuffer(engine.device, &bufferInfo, nullptr, &buffer), "create buffer");
  VkMemoryRequirements requirements{};
  vkGetBufferMemoryRequirements(engine.device, buffer, &requirements);
  VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocation.allocationSize = requirements.size;
  allocation.memoryTypeIndex =
      findMemoryType(engine, requirements.memoryTypeBits, properties);
  check(vkAllocateMemory(engine.device, &allocation, nullptr, &memory),
        "allocate buffer memory");
  check(vkBindBufferMemory(engine.device, buffer, memory, 0), "bind buffer memory");
}

void createImage(const Renderer &engine,
                 VkExtent2D extent,
                 VkFormat format,
                 VkImageUsageFlags usage,
                 VkImage &image,
                 VkDeviceMemory &memory) {
  VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  info.imageType = VK_IMAGE_TYPE_2D;
  info.extent = {extent.width, extent.height, 1};
  info.mipLevels = 1;
  info.arrayLayers = 1;
  info.format = format;
  info.tiling = VK_IMAGE_TILING_OPTIMAL;
  info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  info.usage = usage;
  info.samples = VK_SAMPLE_COUNT_1_BIT;
  info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  check(vkCreateImage(engine.device, &info, nullptr, &image), "create image");
  VkMemoryRequirements requirements{};
  vkGetImageMemoryRequirements(engine.device, image, &requirements);
  VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocation.allocationSize = requirements.size;
  allocation.memoryTypeIndex = findMemoryType(
      engine, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  check(vkAllocateMemory(engine.device, &allocation, nullptr, &memory),
        "allocate image memory");
  check(vkBindImageMemory(engine.device, image, memory, 0), "bind image memory");
}

VkImageView createImageView(const Renderer &engine,
                            VkImage image,
                            VkFormat format,
                            VkImageAspectFlags aspect) {
  VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  info.image = image;
  info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  info.format = format;
  info.subresourceRange.aspectMask = aspect;
  info.subresourceRange.levelCount = 1;
  info.subresourceRange.layerCount = 1;
  VkImageView view = VK_NULL_HANDLE;
  check(vkCreateImageView(engine.device, &info, nullptr, &view), "create image view");
  return view;
}

VkFormat findDepthFormat(const Renderer &engine) {
  const std::array candidates{
      VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT};
  for (VkFormat format : candidates) {
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(engine.physicalDevice, format, &properties);
    if (properties.optimalTilingFeatures &
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
      return format;
    }
  }
  throw std::runtime_error("No supported depth format");
}

VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &formats) {
  const auto preferred =
      std::find_if(formats.begin(), formats.end(), [](const auto &format) {
        return format.format == VK_FORMAT_B8G8R8A8_SRGB &&
               format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
      });
  return preferred != formats.end() ? *preferred : formats.front();
}

VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR> &modes) {
  return std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != modes.end()
             ? VK_PRESENT_MODE_MAILBOX_KHR
             : VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D chooseExtent(const Renderer &engine,
                        const VkSurfaceCapabilitiesKHR &capabilities) {
  if (capabilities.currentExtent.width != UINT32_MAX) {
    return capabilities.currentExtent;
  }
  int width = 0, height = 0;
  glfwGetFramebufferSize(engine.window, &width, &height);
  return {std::clamp(static_cast<uint32_t>(width),
                     capabilities.minImageExtent.width,
                     capabilities.maxImageExtent.width),
          std::clamp(static_cast<uint32_t>(height),
                     capabilities.minImageExtent.height,
                     capabilities.maxImageExtent.height)};
}

void createSceneResources(Renderer &engine) {
  VkDescriptorSetLayoutBinding binding{};
  binding.binding = 0;
  binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  binding.descriptorCount = 1;
  binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  VkDescriptorSetLayoutCreateInfo layout{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  layout.bindingCount = 1;
  layout.pBindings = &binding;
  check(vkCreateDescriptorSetLayout(
            engine.device, &layout, nullptr, &engine.sceneSetLayout),
        "create scene set layout");
  VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                framesInFlight * viewCount};
  VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pool.maxSets = framesInFlight * viewCount;
  pool.poolSizeCount = 1;
  pool.pPoolSizes = &poolSize;
  check(
      vkCreateDescriptorPool(engine.device, &pool, nullptr, &engine.sceneDescriptorPool),
      "create scene descriptor pool");
  for (uint32_t frame = 0; frame < framesInFlight; ++frame) {
    for (uint32_t view = 0; view < viewCount; ++view) {
      createBuffer(engine,
                   sizeof(UniformData),
                   VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                   engine.sceneUniforms[frame][view].buffer,
                   engine.sceneUniforms[frame][view].memory);
      check(vkMapMemory(engine.device,
                        engine.sceneUniforms[frame][view].memory,
                        0,
                        sizeof(UniformData),
                        0,
                        &engine.sceneUniforms[frame][view].mapped),
            "map uniform buffer");
    }
  }
  std::array<VkDescriptorSetLayout, framesInFlight * viewCount> layouts{};
  layouts.fill(engine.sceneSetLayout);
  VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocation.descriptorPool = engine.sceneDescriptorPool;
  allocation.descriptorSetCount = framesInFlight * viewCount;
  allocation.pSetLayouts = layouts.data();
  std::array<VkDescriptorSet, framesInFlight * viewCount> descriptorSets{};
  check(vkAllocateDescriptorSets(engine.device, &allocation, descriptorSets.data()),
        "allocate scene descriptor sets");
  for (uint32_t frame = 0; frame < framesInFlight; ++frame) {
    for (uint32_t view = 0; view < viewCount; ++view) {
      SceneUniform &uniform = engine.sceneUniforms[frame][view];
      uniform.descriptorSet = descriptorSets[frame * viewCount + view];
      VkDescriptorBufferInfo buffer{uniform.buffer, 0, sizeof(UniformData)};
      VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      write.dstSet = uniform.descriptorSet;
      write.dstBinding = 0;
      write.descriptorCount = 1;
      write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      write.pBufferInfo = &buffer;
      vkUpdateDescriptorSets(engine.device, 1, &write, 0, nullptr);
    }
  }
}

void destroySceneResources(Renderer &engine) {
  for (auto &frame : engine.sceneUniforms) {
    for (SceneUniform &uniform : frame) {
      if (uniform.mapped) {
        vkUnmapMemory(engine.device, uniform.memory);
      }
      vkDestroyBuffer(engine.device, uniform.buffer, nullptr);
      vkFreeMemory(engine.device, uniform.memory, nullptr);
      uniform = {};
    }
  }
  vkDestroyDescriptorPool(engine.device, engine.sceneDescriptorPool, nullptr);
  vkDestroyDescriptorSetLayout(engine.device, engine.sceneSetLayout, nullptr);
}

void createRenderPass(Renderer &engine) {
  VkAttachmentDescription color{};
  color.format = engine.swapchainFormat;
  color.samples = VK_SAMPLE_COUNT_1_BIT;
  color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  VkAttachmentDescription depth{};
  depth.format = engine.depthFormat;
  depth.samples = VK_SAMPLE_COUNT_1_BIT;
  depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  const std::array attachments{color, depth};
  VkAttachmentReference colorReference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference depthReference{1,
                                       VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorReference;
  subpass.pDepthStencilAttachment = &depthReference;
  VkSubpassDependency dependency{};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.dstStageMask = dependency.srcStageMask;
  dependency.dstAccessMask =
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  info.attachmentCount = static_cast<uint32_t>(attachments.size());
  info.pAttachments = attachments.data();
  info.subpassCount = 1;
  info.pSubpasses = &subpass;
  info.dependencyCount = 1;
  info.pDependencies = &dependency;
  check(vkCreateRenderPass(engine.device, &info, nullptr, &engine.renderPass),
        "create render pass");
}

void createViewRenderPass(Renderer &engine) {
  VkAttachmentDescription color{};
  color.format = engine.swapchainFormat;
  color.samples = VK_SAMPLE_COUNT_1_BIT;
  color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkAttachmentDescription depth{};
  depth.format = engine.depthFormat;
  depth.samples = VK_SAMPLE_COUNT_1_BIT;
  depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  const std::array attachments{color, depth};
  VkAttachmentReference colorReference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference depthReference{1,
                                       VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorReference;
  subpass.pDepthStencilAttachment = &depthReference;
  const std::array dependencies{
      VkSubpassDependency{VK_SUBPASS_EXTERNAL,
                          0,
                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                          0,
                          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                          0},
      VkSubpassDependency{0,
                          VK_SUBPASS_EXTERNAL,
                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                          VK_ACCESS_SHADER_READ_BIT,
                          0}};
  VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  info.attachmentCount = static_cast<uint32_t>(attachments.size());
  info.pAttachments = attachments.data();
  info.subpassCount = 1;
  info.pSubpasses = &subpass;
  info.dependencyCount = static_cast<uint32_t>(dependencies.size());
  info.pDependencies = dependencies.data();
  check(vkCreateRenderPass(engine.device, &info, nullptr, &engine.viewRenderPass),
        "create view render pass");
}

VkPipelineLayout createPipelineLayout(const Renderer &engine) {
  VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layout.setLayoutCount = 1;
  layout.pSetLayouts = &engine.sceneSetLayout;
  VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
  check(vkCreatePipelineLayout(engine.device, &layout, nullptr, &pipelineLayout),
        "create pipeline layout");
  return pipelineLayout;
}

VkPipeline createPipeline(const Renderer &engine,
                          VkPipelineLayout pipelineLayout,
                          const std::string &shaderName,
                          VkPrimitiveTopology topology,
                          bool writeDepth) {
  const std::string shaderPath = std::string(SHADER_DIRECTORY) + "/" + shaderName;
  const VkShaderModule vertex = createShaderModule(engine, shaderPath + ".vert.spv");
  VkShaderModule fragment = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  try {
    fragment = createShaderModule(engine, shaderPath + ".frag.spv");
    const std::array stages{VkPipelineShaderStageCreateInfo{
                                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                nullptr,
                                0,
                                VK_SHADER_STAGE_VERTEX_BIT,
                                vertex,
                                "main"},
                            VkPipelineShaderStageCreateInfo{
                                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                nullptr,
                                0,
                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                fragment,
                                "main"}};
    VkPipelineVertexInputStateCreateInfo vertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = topology;
    VkPipelineViewportStateCreateInfo viewport{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rasterizer{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisampling{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = writeDepth ? VK_TRUE : VK_FALSE;
    depth.depthCompareOp = writeDepth ? VK_COMPARE_OP_LESS : VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blending{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blending.attachmentCount = 1;
    blending.pAttachments = &blendAttachment;
    const std::array dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamic.pDynamicStates = dynamicStates.data();
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = static_cast<uint32_t>(stages.size());
    info.pStages = stages.data();
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &rasterizer;
    info.pMultisampleState = &multisampling;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blending;
    info.pDynamicState = &dynamic;
    info.layout = pipelineLayout;
    info.renderPass = engine.viewRenderPass;
    check(vkCreateGraphicsPipelines(
              engine.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline),
          "create graphics pipeline");
    vkDestroyShaderModule(engine.device, fragment, nullptr);
    vkDestroyShaderModule(engine.device, vertex, nullptr);
    return pipeline;
  } catch (...) {
    vkDestroyPipeline(engine.device, pipeline, nullptr);
    vkDestroyShaderModule(engine.device, fragment, nullptr);
    vkDestroyShaderModule(engine.device, vertex, nullptr);
    throw;
  }
}

void createPipelines(Renderer &engine) {
  VkPipelineLayout layout = createPipelineLayout(engine);
  VkPipeline cube = VK_NULL_HANDLE;
  VkPipeline grid = VK_NULL_HANDLE;
  try {
    cube =
        createPipeline(engine, layout, "cube", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true);
    grid = createPipeline(engine, layout, "grid", VK_PRIMITIVE_TOPOLOGY_LINE_LIST, false);
  } catch (...) {
    vkDestroyPipeline(engine.device, cube, nullptr);
    vkDestroyPipeline(engine.device, grid, nullptr);
    vkDestroyPipelineLayout(engine.device, layout, nullptr);
    throw;
  }
  engine.pipelineLayout = layout;
  engine.cubePipeline = cube;
  engine.gridPipeline = grid;
}

std::array<std::string, shaderFileCount> shaderPaths() {
  const std::string directory = SHADER_DIRECTORY;
  return {directory + "/cube.vert.spv",
          directory + "/cube.frag.spv",
          directory + "/grid.vert.spv",
          directory + "/grid.frag.spv"};
}

ShaderFileState readShaderFileState(const std::string &path) {
  std::error_code error;
  const auto modified = std::filesystem::last_write_time(path, error);
  if (error) {
    return {};
  }
  return {modified, true};
}

void rememberShaderFiles(Renderer &engine) {
  const auto paths = shaderPaths();
  for (uint32_t index = 0; index < shaderFileCount; ++index) {
    engine.shaderFiles[index] = readShaderFileState(paths[index]);
  }
}

bool shaderFilesChanged(const Renderer &engine) {
  const auto paths = shaderPaths();
  for (uint32_t index = 0; index < shaderFileCount; ++index) {
    if (readShaderFileState(paths[index]) != engine.shaderFiles[index]) {
      return true;
    }
  }
  return false;
}

bool reloadShaders(Renderer &engine) {
  try {
    check(vkDeviceWaitIdle(engine.device), "wait to reload shaders");
    const VkPipeline oldCube = engine.cubePipeline;
    const VkPipeline oldGrid = engine.gridPipeline;
    const VkPipelineLayout oldLayout = engine.pipelineLayout;

    // Only replace live pipelines after both new pipelines have been created.
    createPipelines(engine);
    vkDestroyPipeline(engine.device, oldCube, nullptr);
    vkDestroyPipeline(engine.device, oldGrid, nullptr);
    vkDestroyPipelineLayout(engine.device, oldLayout, nullptr);
    engine.shaderReloadStatus = "Shaders reloaded";
    return true;
  } catch (const std::exception &error) {
    engine.shaderReloadStatus = std::string("Shader reload failed: ") + error.what();
    return false;
  }
}

} // namespace

void pollShaderReload(Renderer &engine, double now, bool requested) {
  const bool shouldPoll = now - engine.lastShaderPollTime >= 0.25;
  const bool changed = shouldPoll && shaderFilesChanged(engine);
  if (shouldPoll) {
    engine.lastShaderPollTime = now;
  }
  if (!requested && !changed) {
    return;
  }
  engine.lastShaderPollTime = now;
  reloadShaders(engine);
  rememberShaderFiles(engine);
}

namespace {

void createDepthTargets(Renderer &engine) {
  engine.depthFormat = findDepthFormat(engine);
  engine.depthTargets.resize(engine.swapchainImages.size());
  for (DepthTarget &target : engine.depthTargets) {
    createImage(engine,
                engine.swapchainExtent,
                engine.depthFormat,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                target.image,
                target.memory);
    target.view = createImageView(
        engine, target.image, engine.depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
  }
}

void createViewTargets(Renderer &engine) {
  for (uint32_t view = 0; view < viewCount; ++view) {
    ViewTarget &target = engine.views[view];
    target.extent = target.requestedExtent;
    createImage(engine,
                target.extent,
                engine.swapchainFormat,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                target.colorImage,
                target.colorMemory);
    target.colorView = createImageView(
        engine, target.colorImage, engine.swapchainFormat, VK_IMAGE_ASPECT_COLOR_BIT);
    createImage(engine,
                target.extent,
                engine.depthFormat,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                target.depthImage,
                target.depthMemory);
    target.depthView = createImageView(
        engine, target.depthImage, engine.depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
    const std::array attachments{target.colorView, target.depthView};
    VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebuffer.renderPass = engine.viewRenderPass;
    framebuffer.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebuffer.pAttachments = attachments.data();
    framebuffer.width = target.extent.width;
    framebuffer.height = target.extent.height;
    framebuffer.layers = 1;
    check(vkCreateFramebuffer(engine.device, &framebuffer, nullptr, &target.framebuffer),
          "create view framebuffer");
  }
}

void destroyViewTargets(Renderer &engine) {
  for (ViewTarget &target : engine.views) {
    vkDestroyFramebuffer(engine.device, target.framebuffer, nullptr);
    vkDestroyImageView(engine.device, target.depthView, nullptr);
    vkDestroyImage(engine.device, target.depthImage, nullptr);
    vkFreeMemory(engine.device, target.depthMemory, nullptr);
    vkDestroyImageView(engine.device, target.colorView, nullptr);
    vkDestroyImage(engine.device, target.colorImage, nullptr);
    vkFreeMemory(engine.device, target.colorMemory, nullptr);
    const VkExtent2D requestedExtent = target.requestedExtent;
    target = {};
    target.requestedExtent = requestedExtent;
  }
}

} // namespace

void resizeViewTargets(Renderer &engine) {
  bool changed = false;
  for (const ViewTarget &target : engine.views) {
    if (target.extent.width != target.requestedExtent.width ||
        target.extent.height != target.requestedExtent.height) {
      changed = true;
      break;
    }
  }
  if (!engine.imguiReady || !changed) {
    return;
  }
  check(vkDeviceWaitIdle(engine.device), "wait to resize views");
  for (ViewTarget &target : engine.views) {
    if (target.texture) {
      ImGui_ImplVulkan_RemoveTexture(target.texture);
    }
  }
  destroyViewTargets(engine);
  createViewTargets(engine);
  for (ViewTarget &target : engine.views) {
    target.texture = ImGui_ImplVulkan_AddTexture(
        target.colorView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  }
}

namespace {

void destroySwapchain(Renderer &engine) {
  if (!engine.commandBuffers.empty()) {
    vkFreeCommandBuffers(engine.device,
                         engine.commandPool,
                         static_cast<uint32_t>(engine.commandBuffers.size()),
                         engine.commandBuffers.data());
  }
  engine.commandBuffers.clear();
  destroyViewTargets(engine);
  for (VkFramebuffer framebuffer : engine.framebuffers) {
    vkDestroyFramebuffer(engine.device, framebuffer, nullptr);
  }
  engine.framebuffers.clear();
  vkDestroyPipeline(engine.device, engine.cubePipeline, nullptr);
  vkDestroyPipeline(engine.device, engine.gridPipeline, nullptr);
  vkDestroyPipelineLayout(engine.device, engine.pipelineLayout, nullptr);
  vkDestroyRenderPass(engine.device, engine.renderPass, nullptr);
  vkDestroyRenderPass(engine.device, engine.viewRenderPass, nullptr);
  engine.cubePipeline = VK_NULL_HANDLE;
  engine.gridPipeline = VK_NULL_HANDLE;
  engine.pipelineLayout = VK_NULL_HANDLE;
  engine.renderPass = VK_NULL_HANDLE;
  engine.viewRenderPass = VK_NULL_HANDLE;
  for (DepthTarget target : engine.depthTargets) {
    vkDestroyImageView(engine.device, target.view, nullptr);
    vkDestroyImage(engine.device, target.image, nullptr);
    vkFreeMemory(engine.device, target.memory, nullptr);
  }
  engine.depthTargets.clear();
  for (VkImageView view : engine.imageViews) {
    vkDestroyImageView(engine.device, view, nullptr);
  }
  engine.imageViews.clear();
  vkDestroySwapchainKHR(engine.device, engine.swapchain, nullptr);
  engine.swapchain = VK_NULL_HANDLE;
}

void createSwapchain(Renderer &engine) {
  const SwapchainSupport support =
      getSwapchainSupport(engine.physicalDevice, engine.surface);
  const VkSurfaceFormatKHR format = chooseSurfaceFormat(support.formats);
  const VkExtent2D extent = chooseExtent(engine, support.capabilities);
  uint32_t imageCount = support.capabilities.minImageCount + 1;
  if (support.capabilities.maxImageCount > 0) {
    imageCount = std::min(imageCount, support.capabilities.maxImageCount);
  }
  const uint32_t families[] = {engine.queues.graphics, engine.queues.present};
  VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  info.surface = engine.surface;
  info.minImageCount = imageCount;
  info.imageFormat = format.format;
  info.imageColorSpace = format.colorSpace;
  info.imageExtent = extent;
  info.imageArrayLayers = 1;
  info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  info.imageSharingMode = engine.queues.graphics == engine.queues.present
                              ? VK_SHARING_MODE_EXCLUSIVE
                              : VK_SHARING_MODE_CONCURRENT;
  info.queueFamilyIndexCount = engine.queues.graphics == engine.queues.present ? 0 : 2;
  info.pQueueFamilyIndices = families;
  info.preTransform = support.capabilities.currentTransform;
  info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  info.presentMode = choosePresentMode(support.presentModes);
  info.clipped = VK_TRUE;
  check(vkCreateSwapchainKHR(engine.device, &info, nullptr, &engine.swapchain),
        "create swapchain");
  engine.swapchainFormat = format.format;
  engine.swapchainExtent = extent;
  vkGetSwapchainImagesKHR(engine.device, engine.swapchain, &imageCount, nullptr);
  engine.swapchainImages.resize(imageCount);
  vkGetSwapchainImagesKHR(
      engine.device, engine.swapchain, &imageCount, engine.swapchainImages.data());
  engine.imageViews.resize(imageCount);
  for (uint32_t i = 0; i < imageCount; ++i) {
    engine.imageViews[i] = createImageView(engine,
                                           engine.swapchainImages[i],
                                           engine.swapchainFormat,
                                           VK_IMAGE_ASPECT_COLOR_BIT);
  }
  createDepthTargets(engine);
  createRenderPass(engine);
  createViewRenderPass(engine);
  createViewTargets(engine);
  createPipelines(engine);
  engine.framebuffers.resize(imageCount);
  for (uint32_t i = 0; i < imageCount; ++i) {
    const std::array attachments{engine.imageViews[i], engine.depthTargets[i].view};
    VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebuffer.renderPass = engine.renderPass;
    framebuffer.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebuffer.pAttachments = attachments.data();
    framebuffer.width = extent.width;
    framebuffer.height = extent.height;
    framebuffer.layers = 1;
    check(vkCreateFramebuffer(
              engine.device, &framebuffer, nullptr, &engine.framebuffers[i]),
          "create framebuffer");
  }
  engine.commandBuffers.resize(imageCount);
  VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  allocation.commandPool = engine.commandPool;
  allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocation.commandBufferCount = imageCount;
  check(
      vkAllocateCommandBuffers(engine.device, &allocation, engine.commandBuffers.data()),
      "allocate command buffers");
}

void createImGuiPool(Renderer &engine) {
  const std::array sizes{
      VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
      VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
      VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
      VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
      VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
      VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
      VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
      VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
      VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
      VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
      VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};
  VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  info.maxSets = 11000;
  info.poolSizeCount = static_cast<uint32_t>(sizes.size());
  info.pPoolSizes = sizes.data();
  check(
      vkCreateDescriptorPool(engine.device, &info, nullptr, &engine.imguiDescriptorPool),
      "create ImGui descriptor pool");
}

void initializeImGui(Renderer &engine) {
  IMGUI_CHECKVERSION();
  if (!ImGui::GetCurrentContext()) {
    ImGui::CreateContext();
  }
  // Presets are read-only; never load or save the working-directory imgui.ini.
  ImGui::GetIO().IniFilename = nullptr;
  ImGui::GetIO().ConfigFlags |=
      ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NoMouseCursorChange;
  ImGui::StyleColorsDark();
  if (!ImGui_ImplGlfw_InitForVulkan(engine.window, true)) {
    throw std::runtime_error("Cannot initialize ImGui GLFW backend");
  }
  engine.imguiGlfwReady = true;
  ImGui_ImplVulkan_InitInfo init{};
  init.Instance = engine.instance;
  init.PhysicalDevice = engine.physicalDevice;
  init.Device = engine.device;
  init.QueueFamily = engine.queues.graphics;
  init.Queue = engine.graphicsQueue;
  init.DescriptorPool = engine.imguiDescriptorPool;
  init.MinImageCount = static_cast<uint32_t>(engine.swapchainImages.size());
  init.ImageCount = static_cast<uint32_t>(engine.swapchainImages.size());
  init.ApiVersion = VK_API_VERSION_1_1;
  init.PipelineInfoMain.RenderPass = engine.renderPass;
  init.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  if (!ImGui_ImplVulkan_Init(&init)) {
    throw std::runtime_error("Cannot initialize ImGui Vulkan backend");
  }
  engine.imguiReady = true;
  for (ViewTarget &target : engine.views) {
    target.texture = ImGui_ImplVulkan_AddTexture(
        target.colorView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  }
  engine.imguiReady = true;
}

void shutdownImGui(Renderer &engine, bool destroyContext = true) {
  if (engine.imguiReady) {
    ImGui_ImplVulkan_Shutdown();
  }
  if (engine.imguiGlfwReady) {
    ImGui_ImplGlfw_Shutdown();
  }
  if (destroyContext && ImGui::GetCurrentContext()) {
    ImGui::DestroyContext();
  }
  engine.imguiReady = false;
  engine.imguiGlfwReady = false;
}

void updateUniform(Renderer &engine,
                   const Scene &scene,
                   uint32_t frame,
                   ViewKind viewKind) {
  const uint32_t viewIndex = static_cast<uint32_t>(viewKind);
  const bool gameView = viewKind == ViewKind::Game;
  math::Mat4 model = math::translation(scene.cube.position);
  if (gameView) {
    const math::Quaternion orientation =
        math::axisAngle({0.3f, 1.0f, 0.15f}, scene.cube.angle);
    model = math::multiply(math::rotation(orientation), model);
  }
  const Camera &camera = gameView ? scene.gameCamera : scene.sceneCamera;
  const math::Vec3 forward = math::rotate(camera.orientation, {0.0f, 0.0f, -1.0f});
  const math::Vec3 up = math::rotate(camera.orientation, {0.0f, 1.0f, 0.0f});
  const math::Mat4 view = math::lookAt(camera.position, forward, up);
  const VkExtent2D extent = engine.views[viewIndex].extent;
  const float aspect =
      static_cast<float>(extent.width) / static_cast<float>(extent.height);
  const float fovRadians = math::radians(camera.fieldOfView);
  const math::Mat4 projection = math::perspective(fovRadians, aspect, 0.1f, 100.0f);
  UniformData uniform{};
  uniform.viewProjection = math::multiply(view, projection);
  uniform.mvp = math::multiply(model, uniform.viewProjection);
  uniform.globalLight = scene.globalLight.enabled ? scene.globalLight.level : 0.0f;
  std::memcpy(engine.sceneUniforms[frame][viewIndex].mapped, &uniform, sizeof(uniform));
}

void recordView(const Renderer &engine, VkCommandBuffer command, ViewKind viewKind) {
  const uint32_t viewIndex = static_cast<uint32_t>(viewKind);
  std::array<VkClearValue, 2> clear{};
  clear[0].color = {{0.02f, 0.025f, 0.05f, 1.0f}};
  clear[1].depthStencil = {1.0f, 0};
  VkRenderPassBeginInfo render{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  render.renderPass = engine.viewRenderPass;
  render.framebuffer = engine.views[viewIndex].framebuffer;
  render.renderArea.extent = engine.views[viewIndex].extent;
  render.clearValueCount = static_cast<uint32_t>(clear.size());
  render.pClearValues = clear.data();
  vkCmdBeginRenderPass(command, &render, VK_SUBPASS_CONTENTS_INLINE);
  const VkExtent2D extent = engine.views[viewIndex].extent;
  const VkViewport viewport{0.0f,
                            static_cast<float>(extent.height),
                            static_cast<float>(extent.width),
                            -static_cast<float>(extent.height),
                            0.0f,
                            1.0f};
  const VkRect2D scissor{{0, 0}, extent};
  vkCmdSetViewport(command, 0, 1, &viewport);
  vkCmdSetScissor(command, 0, 1, &scissor);
  const VkDescriptorSet descriptorSet =
      engine.sceneUniforms[engine.frameIndex][viewIndex].descriptorSet;
  vkCmdBindDescriptorSets(command,
                          VK_PIPELINE_BIND_POINT_GRAPHICS,
                          engine.pipelineLayout,
                          0,
                          1,
                          &descriptorSet,
                          0,
                          nullptr);
  if (viewKind == ViewKind::Scene) {
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, engine.gridPipeline);
    vkCmdDraw(command, gridVertexCount, 1, 0, 0);
  }
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, engine.cubePipeline);
  vkCmdDraw(command, cubeVertexCount, 1, 0, 0);
  vkCmdEndRenderPass(command);
}

void recordCommands(const Renderer &engine, uint32_t imageIndex) {
  const VkCommandBuffer command = engine.commandBuffers[imageIndex];
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  check(vkBeginCommandBuffer(command, &begin), "begin command buffer");
  recordView(engine, command, ViewKind::Scene);
  recordView(engine, command, ViewKind::Game);
  std::array<VkClearValue, 2> clear{};
  clear[0].color = {{0.02f, 0.025f, 0.05f, 1.0f}};
  clear[1].depthStencil = {1.0f, 0};
  VkRenderPassBeginInfo render{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  render.renderPass = engine.renderPass;
  render.framebuffer = engine.framebuffers[imageIndex];
  render.renderArea.extent = engine.swapchainExtent;
  render.clearValueCount = static_cast<uint32_t>(clear.size());
  render.pClearValues = clear.data();
  vkCmdBeginRenderPass(command, &render, VK_SUBPASS_CONTENTS_INLINE);
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command);
  vkCmdEndRenderPass(command);
  check(vkEndCommandBuffer(command), "end command buffer");
}

void recreateSwapchain(Renderer &engine) {
  int width = 0, height = 0;
  glfwGetFramebufferSize(engine.window, &width, &height);
  while (width == 0 || height == 0) {
    if (glfwWindowShouldClose(engine.window)) {
      return;
    }
    glfwWaitEvents();
    glfwGetFramebufferSize(engine.window, &width, &height);
  }
  check(vkDeviceWaitIdle(engine.device), "wait for device");
  // Preserve UI settings and docking state while rebuilding the backends.
  shutdownImGui(engine, false);
  destroySwapchain(engine);
  createSwapchain(engine);
  initializeImGui(engine);
  engine.framebufferResized = false;
}

} // namespace

void drawFrame(Renderer &engine, const Scene &scene) {
  FrameSync &frame = engine.frames[engine.frameIndex];
  check(vkWaitForFences(engine.device,
                        1,
                        &frame.finished,
                        VK_TRUE,
                        std::numeric_limits<uint64_t>::max()),
        "wait for frame");
  uint32_t imageIndex = 0;
  const VkResult acquire = vkAcquireNextImageKHR(engine.device,
                                                 engine.swapchain,
                                                 std::numeric_limits<uint64_t>::max(),
                                                 frame.imageAvailable,
                                                 VK_NULL_HANDLE,
                                                 &imageIndex);
  if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
    recreateSwapchain(engine);
    return;
  }
  if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
    check(acquire, "acquire swapchain image");
  }
  check(vkResetFences(engine.device, 1, &frame.finished), "reset frame fence");
  check(vkResetCommandBuffer(engine.commandBuffers[imageIndex], 0),
        "reset command buffer");
  updateUniform(engine, scene, engine.frameIndex, ViewKind::Scene);
  updateUniform(engine, scene, engine.frameIndex, ViewKind::Game);
  recordCommands(engine, imageIndex);
  const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.waitSemaphoreCount = 1;
  submit.pWaitSemaphores = &frame.imageAvailable;
  submit.pWaitDstStageMask = &waitStage;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &engine.commandBuffers[imageIndex];
  submit.signalSemaphoreCount = 1;
  submit.pSignalSemaphores = &frame.renderFinished;
  check(vkQueueSubmit(engine.graphicsQueue, 1, &submit, frame.finished), "submit draw");
  VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  present.waitSemaphoreCount = 1;
  present.pWaitSemaphores = &frame.renderFinished;
  present.swapchainCount = 1;
  present.pSwapchains = &engine.swapchain;
  present.pImageIndices = &imageIndex;
  const VkResult result = vkQueuePresentKHR(engine.presentQueue, &present);
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
      engine.framebufferResized) {
    recreateSwapchain(engine);
  } else {
    check(result, "present image");
  }
  engine.frameIndex = (engine.frameIndex + 1) % framesInFlight;
}

namespace {

void initializeWindow(Renderer &engine) {
  if (!glfwInit()) {
    throw std::runtime_error("glfwInit failed");
  }
  engine.glfwStarted = true;
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  engine.window =
      glfwCreateWindow(windowWidth, windowHeight, "Blossom Vulkan", nullptr, nullptr);
  if (!engine.window) {
    throw std::runtime_error("glfwCreateWindow failed");
  }
  glfwSetWindowUserPointer(engine.window, &engine);
  glfwSetFramebufferSizeCallback(engine.window, onFramebufferResize);
  glfwSetInputMode(engine.window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

void createInstance(Renderer &engine) {
  uint32_t extensionCount = 0;
  const char **extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "Blossom";
  app.pEngineName = "Blossom";
  // Vulkan 1.1 makes negative viewport height available without an extension.
  app.apiVersion = VK_API_VERSION_1_1;
  VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  instanceInfo.pApplicationInfo = &app;
  instanceInfo.enabledExtensionCount = extensionCount;
  instanceInfo.ppEnabledExtensionNames = extensions;
  check(vkCreateInstance(&instanceInfo, nullptr, &engine.instance),
        "create Vulkan instance");
  check(glfwCreateWindowSurface(engine.instance, engine.window, nullptr, &engine.surface),
        "create GLFW surface");
}

void selectPhysicalDevice(Renderer &engine) {
  uint32_t deviceCount = 0;
  vkEnumeratePhysicalDevices(engine.instance, &deviceCount, nullptr);
  std::vector<VkPhysicalDevice> devices(deviceCount);
  vkEnumeratePhysicalDevices(engine.instance, &deviceCount, devices.data());
  for (VkPhysicalDevice device : devices) {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device, &properties);
    if (properties.apiVersion < VK_API_VERSION_1_1) {
      continue;
    }
    QueueFamilies queues{};
    if (supportsDevice(device, engine.surface, queues)) {
      engine.physicalDevice = device;
      engine.queues = queues;
      break;
    }
  }
  if (!engine.physicalDevice) {
    throw std::runtime_error("No Vulkan 1.1 device can present to this window");
  }
}

void createDevice(Renderer &engine) {
  const float priority = 1.0f;
  std::vector<uint32_t> families{engine.queues.graphics};
  if (engine.queues.present != engine.queues.graphics) {
    families.push_back(engine.queues.present);
  }
  std::vector<VkDeviceQueueCreateInfo> queues;
  for (uint32_t family : families) {
    VkDeviceQueueCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    info.queueFamilyIndex = family;
    info.queueCount = 1;
    info.pQueuePriorities = &priority;
    queues.push_back(info);
  }
  const char *extensionsForDevice[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  deviceInfo.queueCreateInfoCount = static_cast<uint32_t>(queues.size());
  deviceInfo.pQueueCreateInfos = queues.data();
  deviceInfo.enabledExtensionCount = 1;
  deviceInfo.ppEnabledExtensionNames = extensionsForDevice;
  check(vkCreateDevice(engine.physicalDevice, &deviceInfo, nullptr, &engine.device),
        "create logical device");
  vkGetDeviceQueue(engine.device, engine.queues.graphics, 0, &engine.graphicsQueue);
  vkGetDeviceQueue(engine.device, engine.queues.present, 0, &engine.presentQueue);
}

void createCommandPool(Renderer &engine) {
  VkCommandPoolCreateInfo commandPool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  commandPool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  commandPool.queueFamilyIndex = engine.queues.graphics;
  check(vkCreateCommandPool(engine.device, &commandPool, nullptr, &engine.commandPool),
        "create command pool");
}

void createFrameSync(Renderer &engine) {
  VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  for (FrameSync &frame : engine.frames) {
    check(vkCreateSemaphore(engine.device, &semaphore, nullptr, &frame.imageAvailable),
          "create image semaphore");
    check(vkCreateSemaphore(engine.device, &semaphore, nullptr, &frame.renderFinished),
          "create render semaphore");
    check(vkCreateFence(engine.device, &fence, nullptr, &frame.finished),
          "create frame fence");
  }
}

} // namespace

void initializeRenderer(Renderer &engine) {
  initializeWindow(engine);
  createInstance(engine);
  selectPhysicalDevice(engine);
  createDevice(engine);

  createCommandPool(engine);
  createSceneResources(engine);
  createImGuiPool(engine);
  createSwapchain(engine);
  createFrameSync(engine);
  initializeImGui(engine);

  rememberShaderFiles(engine);
}

void shutdownRenderer(Renderer &engine) {
  if (engine.device) {
    vkDeviceWaitIdle(engine.device);
  }
  shutdownImGui(engine);
  if (engine.device) {
    for (const FrameSync &frame : engine.frames) {
      vkDestroyFence(engine.device, frame.finished, nullptr);
      vkDestroySemaphore(engine.device, frame.renderFinished, nullptr);
      vkDestroySemaphore(engine.device, frame.imageAvailable, nullptr);
    }
    destroySwapchain(engine);
    vkDestroyDescriptorPool(engine.device, engine.imguiDescriptorPool, nullptr);
    destroySceneResources(engine);
    vkDestroyCommandPool(engine.device, engine.commandPool, nullptr);
    vkDestroyDevice(engine.device, nullptr);
  }
  if (engine.surface) {
    vkDestroySurfaceKHR(engine.instance, engine.surface, nullptr);
  }
  if (engine.instance) {
    vkDestroyInstance(engine.instance, nullptr);
  }
  if (engine.window) {
    glfwDestroyWindow(engine.window);
  }
  if (engine.glfwStarted) {
    glfwTerminate();
  }
}
