
#pragma once

#include <cmath>

namespace util {

inline float deg2rad(float degrees) {
    return degrees * (M_PI / 180.0f);
}

inline float rad2deg(float radians) {
    return radians * (180.0f / M_PI);
}

inline float trunc(float value, int places) {
    float factor = powf(10.0f, (float)places);
    return floorf(value * factor) / factor;
}

inline float clamp(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

inline int clamp(int value, int min, int max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

inline float lerp(float a, float b, float t) {
    return a + t * (b - a);
}

inline float sign(float value) {
    if (value > 0.0f) return 1.0f;
    if (value < 0.0f) return -1.0f;
    return 0.0f;
}

}  // namespace util

