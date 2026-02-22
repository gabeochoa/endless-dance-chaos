#pragma once

// Shared helpers used by all render system domain files.
// Provides font loading, day/night palette, tile colors.

#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "afterhours/src/drawing_helpers.h"
#include "afterhours/src/drawing_helpers_3d.h"
#include "afterhours/src/font_helper.h"
#include "afterhours/src/graphics.h"
#include "components.h"

using namespace afterhours;

// Render texture from main.cpp for MCP screenshots
extern afterhours::graphics::RenderTextureType g_render_texture;

// ── 2D drawing helpers ──
// Functions with matching signatures (draw_text, draw_line, draw_circle,
// draw_text_ex, begin/end_scissor_mode) come from afterhours via
// `using namespace afterhours;`. Only wrappers that adapt signatures remain.

inline void draw_rect(float x, float y, float w, float h, Color c) {
    afterhours::draw_rectangle(Rectangle{x, y, w, h}, c);
}

inline void draw_rect_lines(float x, float y, float w, float h, Color c) {
    afterhours::draw_rectangle_outline(Rectangle{x, y, w, h}, c, 1.f);
}

// ── Window / frame / timing ──

using afterhours::graphics::begin_drawing;
using afterhours::graphics::end_drawing;
using afterhours::graphics::get_frame_time;
using afterhours::graphics::window_should_close;

inline void clear_background(Color c) {
    afterhours::graphics::clear_background(c);
}

inline int get_fps() {
    return static_cast<int>(afterhours::graphics::get_fps());
}
inline float get_time() {
    return static_cast<float>(afterhours::graphics::get_time());
}

inline vec2 measure_text_ex(afterhours::Font font, const char* text, float size,
                            float spacing) {
    return afterhours::measure_text(font, text, size, spacing);
}

static constexpr float FONT_SPACING = 1.5f;

inline afterhours::Font& get_font() {
    static afterhours::Font font{};
    static bool loaded = false;
    if (!loaded) {
        font = afterhours::load_font_from_file("resources/fonts/Exo2-Bold.ttf",
                                               96);
        loaded = true;
    }
    return font;
}

// Day/Night color palette with smoothstep transition
inline float smoothstep(float edge0, float edge1, float x) {
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline Color lerp_color(Color a, Color b, float t) {
    return Color{
        (uint8_t) (a.r + (b.r - a.r) * t), (uint8_t) (a.g + (b.g - a.g) * t),
        (uint8_t) (a.b + (b.b - a.b) * t), (uint8_t) (a.a + (b.a - a.a) * t)};
}

inline float get_day_night_t() {
    auto* clock = EntityHelper::get_singleton_cmp<GameClock>();
    if (!clock) return 0.0f;
    float gm = clock->game_time_minutes;
    int hour = (int) (gm / 60) % 24;
    float minute_in_hour = std::fmod(gm, 60.f);
    if (hour == 17)
        return smoothstep(0, 60, minute_in_hour);
    else if (hour == 9)
        return 1.0f - smoothstep(0, 60, minute_in_hour);
    else if (hour >= 18 || hour < 3)
        return 1.0f;
    else if (hour >= 10 && hour < 17)
        return 0.0f;
    return 1.0f;
}

// Shared day-palette colors for tiles, used by grid rendering and minimap.
inline constexpr Color TILE_DAY_COLORS[] = {
    {152, 212, 168, 255},  // Grass
    {232, 221, 212, 255},  // Path
    {85, 85, 85, 255},     // Fence
    {68, 136, 170, 255},   // Gate
    {255, 217, 61, 255},   // Stage
    {255, 235, 150, 255},  // StageFloor
    {126, 207, 192, 255},  // Bathroom
    {244, 164, 164, 255},  // Food
    {255, 100, 100, 255},  // MedTent
};

inline constexpr Color TILE_NIGHT_COLORS[] = {
    {45, 74, 62, 255},    // Grass
    {42, 42, 58, 255},    // Path
    {40, 40, 50, 255},    // Fence
    {68, 136, 170, 255},  // Gate
    {255, 230, 0, 255},   // Stage
    {60, 60, 40, 255},    // StageFloor
    {60, 100, 90, 255},   // Bathroom
    {120, 80, 80, 255},   // Food
    {120, 50, 50, 255},   // MedTent
};

inline Color tile_day_color(TileType type) {
    return TILE_DAY_COLORS[static_cast<int>(type)];
}

inline Color tile_color(TileType type, float night_t) {
    int idx = static_cast<int>(type);
    return lerp_color(TILE_DAY_COLORS[idx], TILE_NIGHT_COLORS[idx], night_t);
}
