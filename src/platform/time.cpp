#include "platform/time.hpp"

#include <GLFW/glfw3.h>

namespace platform {

double time_seconds() { return glfwGetTime(); }

} // namespace platform
