#pragma once

#include "math/math.hpp"
#include "resources/mesh.hpp"
#include "resources/texture.hpp"

#include <cstdint>
#include <vector>

namespace scene {

// For objects drawn with vertex colors only (a white texture).
inline constexpr uint32_t NO_TEXTURE = UINT32_MAX;

struct Scene {
  std::vector<resources::MeshData> meshes;
  std::vector<resources::TextureData> textures;

  // One entry per object. Texture color is multiplied by vertex color.
  std::vector<uint32_t> object_meshes;
  std::vector<uint32_t> object_textures;
  std::vector<math::Mat4> object_transforms;
};

// A cube, a sphere, and a pyramid side by side, untextured.
Scene make_test_scene();

} // namespace scene
