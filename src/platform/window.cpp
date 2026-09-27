#include "platform/window.hpp"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <print>

namespace platform {

static void glfw_error_callback(int code, const char* description) {
    std::println(stderr, "[glfw] error {}: {}", code, description);
}

bool init() {
    glfwSetErrorCallback(glfw_error_callback);

    if (!glfwInit()) {
        std::println(stderr, "[platform] failed to initialize GLFW");
        return false;
    }

    if (!glfwVulkanSupported()) {
        std::println(stderr, "[platform] Vulkan loader or ICD not found");
        glfwTerminate();
        return false;
    }

    return true;
}

void shutdown() {
    glfwTerminate();
}

bool create_window(Window& window, const WindowConfig& config) {
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, config.isResizable ? GLFW_TRUE : GLFW_FALSE);

    window.handle = glfwCreateWindow(
        static_cast<int>(config.width),
        static_cast<int>(config.height),
        config.title,
        nullptr,
        nullptr
    );

    if (!window.handle) {
        std::println(stderr, "[platform] failed to create window");
        return false;
    }

    window.width = config.width;
    window.height = config.height;
    return true;
}

void destroy_window(Window& window) {
    if (window.handle) {
        glfwDestroyWindow(window.handle);
    }
    window = {};
}

void framebuffer_size(const Window& window, uint32_t& width, uint32_t& height) {
    int pixel_width = 0;
    int pixel_height = 0;
    glfwGetFramebufferSize(window.handle, &pixel_width, &pixel_height);
    width = static_cast<uint32_t>(pixel_width);
    height = static_cast<uint32_t>(pixel_height);
}

bool should_close(const Window& window) {
    return glfwWindowShouldClose(window.handle);
}

void poll_events() {
    glfwPollEvents();
}

}
