#include "scene/scene.hpp"

namespace scene {

Scene make_test_scene() {
  Scene scene;

  scene.meshes.push_back(resources::make_cube({0.9f, 0.3f, 0.3f}));
  scene.meshes.push_back(resources::make_sphere({0.3f, 0.8f, 0.4f}, 32, 16));
  scene.meshes.push_back(resources::make_pyramid({0.3f, 0.5f, 0.9f}));

  scene.object_meshes = {0, 1, 2};
  scene.object_textures = {NO_TEXTURE, NO_TEXTURE, NO_TEXTURE};
  scene.object_transforms = {
      math::translation({-1.6f, 0.0f, 0.0f}) *
          math::rotation({0.0f, 1.0f, 0.0f}, math::radians(35.0f)),
      math::identity(),
      math::translation({1.6f, 0.0f, 0.0f}) *
          math::rotation({0.0f, 1.0f, 0.0f}, math::radians(20.0f)),
  };

  return scene;
}

} // namespace scene
