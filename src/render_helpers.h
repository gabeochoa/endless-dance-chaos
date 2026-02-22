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

// ── 2D drawing helpers ──
// Thin wrappers that keep game code backend-agnostic.
// When the afterhours drawing_helpers are used project-wide,
// these can be replaced with direct afterhours calls.

inline void draw_rect(float x, float y, float w, float h, Color c) {
    raylib::DrawRectangle((int) x, (int) y, (int) w, (int) h, c);
}

inline void draw_rect_lines(float x, float y, float w, float h, Color c) {
    raylib::DrawRectangleLinesEx(Rectangle{x, y, w, h}, 1.f, c);
}

inline void draw_line(float x1, float y1, float x2, float y2, Color c) {
    raylib::DrawLine((int) x1, (int) y1, (int) x2, (int) y2, c);
}

inline void draw_circle(float cx, float cy, float r, Color c) {
    raylib::DrawCircle((int) cx, (int) cy, r, c);
}

inline void draw_text_ex(raylib::Font font, const char* text, vec2 pos,
                         float size, float spacing, Color c) {
    raylib::DrawTextEx(font, text, pos, size, spacing, c);
}

inline void draw_text(const char* text, float x, float y, float size, Color c) {
    raylib::DrawText(text, (int) x, (int) y, (int) size, c);
}

inline void begin_scissor_mode(float x, float y, float w, float h) {
    raylib::BeginScissorMode((int) x, (int) y, (int) w, (int) h);
}

inline void end_scissor_mode() { raylib::EndScissorMode(); }

inline void begin_drawing() { raylib::BeginDrawing(); }
inline void end_drawing() { raylib::EndDrawing(); }

inline void clear_background(Color c) { raylib::ClearBackground(c); }

inline int get_fps() { return raylib::GetFPS(); }
inline float get_frame_time() { return raylib::GetFrameTime(); }
inline float get_time() { return static_cast<float>(raylib::GetTime()); }
inline bool window_should_close() { return raylib::WindowShouldClose(); }

inline vec2 measure_text_ex(raylib::Font font, const char* text, float size,
                            float spacing) {
    return raylib::MeasureTextEx(font, text, size, spacing);
}

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
