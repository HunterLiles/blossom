#pragma once

#include <vulkan/vulkan.h>

#include <filesystem>

namespace vulkan {

struct Context;

// Loads a compiled SPIR-V file. Returns VK_NULL_HANDLE on failure.
VkShaderModule create_shader_module(const Context& context,
                                    const std::filesystem::path& path);

} // namespace vulkan
