#pragma once

#include <cstdint>

struct GLFWwindow;

namespace platform {

struct WindowConfig {
    const char* title = "Blossom";
    uint32_t width = 1280;
    uint32_t height = 720;
    bool isResizable = true;
};

struct Window {
    GLFWwindow* handle = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
};

bool init();
void shutdown();

bool create_window(Window& window, const WindowConfig& config);
void destroy_window(Window& window);

// Size in pixels, which can differ from the window size on HiDPI displays.
void framebuffer_size(const Window& window, uint32_t& width, uint32_t& height);

bool should_close(const Window& window);
void poll_events();

}
