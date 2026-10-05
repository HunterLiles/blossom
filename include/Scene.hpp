#pragma once

#include "Math.hpp"

#include <cstdint>

enum class ViewKind : uint32_t { Scene = 0, Game = 1 };
enum class SceneObject : uint32_t { None, Cube };

struct Camera {
  math::Vec3 position{0.0f, 0.0f, 3.0f};
  math::Quaternion orientation{};
  float fieldOfView = 90.0f;
  float speed = 3.0f;
  float sensitivity = 0.0025f;
};

struct GlobalLight {
  bool enabled = true;
  float level = 1.0f;
};

// Instance state only; geometry is defined in shaders/objects.glsl.
struct CubeState {
  math::Vec3 position{};
  float angle = 0.0f;
};

struct Scene {
  Camera gameCamera{};
  Camera sceneCamera{{0.0f, 1.5f, 5.0f}};
  ViewKind focusedView = ViewKind::Game;
  SceneObject selectedObject = SceneObject::None;
  CubeState cube{};
  GlobalLight globalLight{};
};

inline Camera &focusedCamera(Scene &scene) {
  return scene.focusedView == ViewKind::Scene ? scene.sceneCamera : scene.gameCamera;
}
