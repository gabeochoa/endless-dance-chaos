

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
#define Vector3Type raylib::Vector3
#define TextureType raylib::Texture2D
#define ColorType raylib::Color
#include <afterhours/ah.h>
#include <afterhours/src/developer.h>
#include <afterhours/src/plugins/color.h>
#include <afterhours/src/plugins/input_system.h>
#include <afterhours/src/plugins/texture_manager.h>
#include <afterhours/src/plugins/window_manager.h>

#include <cassert>

typedef Vector2Type vec2;
typedef Vector3Type vec3;
using Color = afterhours::Color;
using Rectangle = RectangleType;

// Re-export afterhours input constants into the global namespace
// so game code can use short names (KEY_SPACE, MOUSE_BUTTON_LEFT, etc.)
namespace ah_keys = afterhours::keys;
namespace ah_mouse = afterhours::mouse_buttons;
namespace ah_gpad = afterhours::gamepad_buttons;
constexpr auto KEY_ESCAPE = ah_keys::ESCAPE;
constexpr auto KEY_SPACE = ah_keys::SPACE;
constexpr auto KEY_ENTER = ah_keys::ENTER;
constexpr auto KEY_TAB = ah_keys::TAB;
constexpr auto KEY_W = ah_keys::W;
constexpr auto KEY_A = ah_keys::A;
constexpr auto KEY_S = ah_keys::S;
constexpr auto KEY_D = ah_keys::D;
constexpr auto KEY_E = ah_keys::E;
constexpr auto KEY_Q = ah_keys::Q;
constexpr auto KEY_X = ah_keys::X;
constexpr auto KEY_UP = ah_keys::UP;
constexpr auto KEY_DOWN = ah_keys::DOWN;
constexpr auto KEY_LEFT = ah_keys::LEFT;
constexpr auto KEY_RIGHT = ah_keys::RIGHT;
constexpr auto KEY_ONE = ah_keys::ONE;
constexpr auto KEY_TWO = ah_keys::TWO;
constexpr auto KEY_THREE = ah_keys::THREE;
constexpr auto KEY_FOUR = ah_keys::FOUR;
constexpr auto KEY_FIVE = ah_keys::FIVE;
constexpr auto KEY_SIX = ah_keys::SIX;
constexpr auto KEY_SEVEN = ah_keys::SEVEN;
constexpr auto KEY_EIGHT = ah_keys::EIGHT;
constexpr auto KEY_GRAVE = ah_keys::GRAVE;
constexpr auto KEY_EQUAL = ah_keys::EQUAL;
constexpr auto KEY_LEFT_BRACKET = ah_keys::LEFT_BRACKET;
constexpr auto KEY_RIGHT_BRACKET = ah_keys::RIGHT_BRACKET;
constexpr auto KEY_LEFT_SHIFT = ah_keys::LEFT_SHIFT;
constexpr auto KEY_F5 = ah_keys::F5;
constexpr auto KEY_F9 = ah_keys::F9;
constexpr auto MOUSE_BUTTON_LEFT = ah_mouse::LEFT;
constexpr auto MOUSE_BUTTON_RIGHT = ah_mouse::RIGHT;
constexpr auto GAMEPAD_BUTTON_RIGHT_FACE_DOWN = ah_gpad::RIGHT_FACE_DOWN;
constexpr auto GAMEPAD_BUTTON_LEFT_FACE_LEFT = ah_gpad::LEFT_FACE_LEFT;
constexpr auto GAMEPAD_BUTTON_LEFT_FACE_RIGHT = ah_gpad::LEFT_FACE_RIGHT;
constexpr auto GAMEPAD_BUTTON_LEFT_FACE_UP = ah_gpad::LEFT_FACE_UP;
constexpr auto GAMEPAD_BUTTON_LEFT_FACE_DOWN = ah_gpad::LEFT_FACE_DOWN;
constexpr auto GAMEPAD_BUTTON_MIDDLE_RIGHT = ah_gpad::MIDDLE_RIGHT;

#ifdef __clang__
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
