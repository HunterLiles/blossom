#include "resources/texture.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <cstdio>
#include <print>

namespace resources {

bool load_texture(TextureData& texture, const std::filesystem::path& path) {
  texture = {};

  int width = 0;
  int height = 0;
  int channels = 0;
  stbi_uc* pixels =
      stbi_load(path.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
  if (!pixels) {
    std::println(stderr, "[resources] failed to load {}: {}", path.string(),
                 stbi_failure_reason());
    return false;
  }

  texture.width = static_cast<uint32_t>(width);
  texture.height = static_cast<uint32_t>(height);
  texture.pixels.assign(pixels, pixels + static_cast<size_t>(width) *
                                             static_cast<size_t>(height) * 4);
  stbi_image_free(pixels);
  return true;
}

TextureData make_solid_texture(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  return {1, 1, {r, g, b, a}};
}

} // namespace resources
