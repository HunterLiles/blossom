#include "render/vulkan/shader.hpp"

#include "render/vulkan/context.hpp"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <print>
#include <vector>

namespace vulkan {

VkShaderModule create_shader_module(const Context& context,
                                    const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    std::println(stderr, "[vulkan] cannot open shader {}", path.string());
    return VK_NULL_HANDLE;
  }

  std::streamsize size = file.tellg();
  if (size <= 0 || size % 4 != 0) {
    std::println(stderr, "[vulkan] {} is not valid SPIR-V", path.string());
    return VK_NULL_HANDLE;
  }

  std::vector<uint32_t> code(static_cast<size_t>(size) / 4);
  file.seekg(0);
  file.read(reinterpret_cast<char*>(code.data()), size);

  VkShaderModuleCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  create_info.codeSize = static_cast<size_t>(size);
  create_info.pCode = code.data();

  VkShaderModule module = VK_NULL_HANDLE;
  VkResult result = vkCreateShaderModule(context.device, &create_info, nullptr, &module);
  if (result != VK_SUCCESS) {
    std::println(stderr, "[vulkan] vkCreateShaderModule failed for {}: {}", path.string(),
                 static_cast<int>(result));
    return VK_NULL_HANDLE;
  }
  return module;
}

} // namespace vulkan
