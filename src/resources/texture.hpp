#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace resources {

// 8-bit RGBA pixels, rows top to bottom. Treated as sRGB color data.
struct TextureData {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> pixels;
};

// Loads PNG, JPEG, TGA, BMP, etc. and expands to RGBA. Returns false and
// leaves `texture` empty on failure.
bool load_texture(TextureData& texture, const std::filesystem::path& path);

// A 1x1 texture of one color.
TextureData make_solid_texture(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

} // namespace resources
