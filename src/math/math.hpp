#pragma once

#include "math/mat.hpp"
#include "math/quat.hpp"
#include "math/vec.hpp"

namespace math {

inline constexpr float PI = 3.14159265358979323846f;

constexpr float radians(float degrees) { return degrees * (PI / 180.0f); }
constexpr float degrees(float radians) { return radians * (180.0f / PI); }

} // namespace math
