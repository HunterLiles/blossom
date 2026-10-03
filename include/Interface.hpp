#pragma once

#include <cstddef>

struct Input;
struct Renderer;
struct Scene;

struct Interface {
  size_t selectedLayout = 0;
  size_t requestedLayout = 0;
  bool layoutChangeRequested = false;
  bool inspectorOpen = false;
};

// Returns a shader reload request from the UI.
bool buildInterface(Interface &interface,
                    Input &input,
                    Scene &scene,
                    Renderer &renderer,
                    float frameTime);
void loadWorkspaceLayout(Interface &interface);
void drawWorkspaceLayoutSelector(Interface &interface);
void beginWorkspaceDockspace();
