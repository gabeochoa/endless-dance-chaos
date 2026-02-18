#pragma once

// Shared helpers used by all render system domain files.
// Provides font loading, day/night palette, tile colors.

#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "components.h"

using namespace afterhours;

// Render texture from main.cpp for MCP screenshots
extern raylib::RenderTexture2D g_render_texture;

static constexpr float FONT_SPACING = 1.5f;

inline raylib::Font& get_font() {
    static raylib::Font font = {0};
    static bool loaded = false;
    if (!loaded) {
        font =
            raylib::LoadFontEx("resources/fonts/Exo2-Bold.ttf", 96, nullptr, 0);
        raylib::SetTextureFilter(font.texture, raylib::TEXTURE_FILTER_BILINEAR);
        loaded = true;
    }
    return font;
}

// Day/Night color palette with smoothstep transition
inline float smoothstep(float edge0, float edge1, float x) {
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline raylib::Color lerp_color(raylib::Color a, raylib::Color b, float t) {
    return raylib::Color{
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
inline constexpr raylib::Color TILE_DAY_COLORS[] = {
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

inline constexpr raylib::Color TILE_NIGHT_COLORS[] = {
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

inline raylib::Color tile_day_color(TileType type) {
    return TILE_DAY_COLORS[static_cast<int>(type)];
}

inline raylib::Color tile_color(TileType type, float night_t) {
    int idx = static_cast<int>(type);
    return lerp_color(TILE_DAY_COLORS[idx], TILE_NIGHT_COLORS[idx], night_t);
}
