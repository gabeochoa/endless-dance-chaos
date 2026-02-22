

#pragma once

#include "std_include.h"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#pragma clang diagnostic ignored "-Wdeprecated-volatile"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wfloat-conversion"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wdangling-reference"
#endif

namespace raylib {
#if defined(__has_include)
#if __has_include(<raylib.h>)
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#elif __has_include("raylib/raylib.h")
#include "raylib/raylib.h"
#include "raylib/raymath.h"
#include "raylib/rlgl.h"
#else
#error "raylib headers not found"
#endif
#else
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#endif

// Scalar-left multiplication overloads
inline Vector2 operator*(float s, Vector2 a) { return Vector2Scale(a, s); }
inline Vector3 operator*(float s, Vector3 a) { return Vector3Scale(a, s); }

// Comparison operators for use in std::set/std::map (required by pathfinding
// plugin)
inline bool operator<(const Vector2& a, const Vector2& b) {
    if (a.x != b.x) return a.x < b.x;
    return a.y < b.y;
}

inline bool operator<(const Vector3& a, const Vector3& b) {
    if (a.x != b.x) return a.x < b.x;
    if (a.y != b.y) return a.y < b.y;
    return a.z < b.z;
}

}  // namespace raylib

#include <GLFW/glfw3.h>

#undef MAGIC_ENUM_RANGE_MAX
#define MAGIC_ENUM_RANGE_MAX 400
#include <magic_enum/magic_enum.hpp>

#define AFTER_HOURS_ENTITY_HELPER
#define AFTER_HOURS_ENTITY_QUERY
#define AFTER_HOURS_SYSTEM
#define AFTER_HOURS_USE_RAYLIB

// Tell afterhours to skip its log functions - we provide our own via log.h
// log.h must come BEFORE afterhours so our log macros are available
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#define RectangleType raylib::Rectangle
#define Vector2Type raylib::Vector2
#define TextureType raylib::Texture2D
#include <afterhours/ah.h>
#include <afterhours/src/developer.h>
#include <afterhours/src/plugins/input_system.h>
#include <afterhours/src/plugins/texture_manager.h>
#include <afterhours/src/plugins/window_manager.h>

#include <cassert>

typedef raylib::Vector2 vec2;
typedef raylib::Vector3 vec3;
typedef raylib::Vector4 vec4;
using raylib::Color;
using raylib::Rectangle;

// Re-export commonly used raylib constants so game code
// doesn't need the raylib:: prefix on key/mouse/camera values.
constexpr auto KEY_ESCAPE = raylib::KEY_ESCAPE;
constexpr auto KEY_SPACE = raylib::KEY_SPACE;
constexpr auto KEY_ENTER = raylib::KEY_ENTER;
constexpr auto KEY_TAB = raylib::KEY_TAB;
constexpr auto KEY_W = raylib::KEY_W;
constexpr auto KEY_A = raylib::KEY_A;
constexpr auto KEY_S = raylib::KEY_S;
constexpr auto KEY_D = raylib::KEY_D;
constexpr auto KEY_E = raylib::KEY_E;
constexpr auto KEY_Q = raylib::KEY_Q;
constexpr auto KEY_X = raylib::KEY_X;
constexpr auto KEY_UP = raylib::KEY_UP;
constexpr auto KEY_DOWN = raylib::KEY_DOWN;
constexpr auto KEY_LEFT = raylib::KEY_LEFT;
constexpr auto KEY_RIGHT = raylib::KEY_RIGHT;
constexpr auto KEY_ONE = raylib::KEY_ONE;
constexpr auto KEY_TWO = raylib::KEY_TWO;
constexpr auto KEY_THREE = raylib::KEY_THREE;
constexpr auto KEY_FOUR = raylib::KEY_FOUR;
constexpr auto KEY_FIVE = raylib::KEY_FIVE;
constexpr auto KEY_SIX = raylib::KEY_SIX;
constexpr auto KEY_SEVEN = raylib::KEY_SEVEN;
constexpr auto KEY_EIGHT = raylib::KEY_EIGHT;
constexpr auto KEY_GRAVE = raylib::KEY_GRAVE;
constexpr auto KEY_EQUAL = raylib::KEY_EQUAL;
constexpr auto KEY_LEFT_BRACKET = raylib::KEY_LEFT_BRACKET;
constexpr auto KEY_RIGHT_BRACKET = raylib::KEY_RIGHT_BRACKET;
constexpr auto KEY_LEFT_SHIFT = raylib::KEY_LEFT_SHIFT;
constexpr auto KEY_F5 = raylib::KEY_F5;
constexpr auto KEY_F9 = raylib::KEY_F9;
constexpr auto MOUSE_BUTTON_LEFT = raylib::MOUSE_BUTTON_LEFT;
constexpr auto MOUSE_BUTTON_RIGHT = raylib::MOUSE_BUTTON_RIGHT;
constexpr auto GAMEPAD_BUTTON_RIGHT_FACE_DOWN =
    raylib::GAMEPAD_BUTTON_RIGHT_FACE_DOWN;
constexpr auto GAMEPAD_BUTTON_LEFT_FACE_LEFT =
    raylib::GAMEPAD_BUTTON_LEFT_FACE_LEFT;
constexpr auto GAMEPAD_BUTTON_LEFT_FACE_RIGHT =
    raylib::GAMEPAD_BUTTON_LEFT_FACE_RIGHT;
constexpr auto GAMEPAD_BUTTON_LEFT_FACE_UP =
    raylib::GAMEPAD_BUTTON_LEFT_FACE_UP;
constexpr auto GAMEPAD_BUTTON_LEFT_FACE_DOWN =
    raylib::GAMEPAD_BUTTON_LEFT_FACE_DOWN;
constexpr auto GAMEPAD_BUTTON_MIDDLE_RIGHT =
    raylib::GAMEPAD_BUTTON_MIDDLE_RIGHT;
constexpr auto CAMERA_PERSPECTIVE = raylib::CAMERA_PERSPECTIVE;
constexpr auto CAMERA_ORTHOGRAPHIC = raylib::CAMERA_ORTHOGRAPHIC;

#ifdef __clang__
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
