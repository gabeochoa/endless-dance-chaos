#pragma once

// Shared helpers used by all render system domain files.
// Provides font loading, day/night palette, tile colors.

#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include <deque>
#include <string>

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

// ── '90s tycoon chrome ──
// Flat gray panel with a classic bevel: light top-left edges, dark
// bottom-right edges. Drawn with the existing rect helpers only.
inline void draw_bevel_panel(float x, float y, float w, float h, Color bg) {
    draw_rect(x, y, w, h, bg);
    draw_rect(x, y, w, 2, Color{255, 255, 255, 255});
    draw_rect(x, y, 2, h, Color{255, 255, 255, 255});
    draw_rect(x, y + h - 2, w, 2, Color{64, 64, 64, 255});
    draw_rect(x + w - 2, y, 2, h, Color{64, 64, 64, 255});
}

// Pressed-in variant for a selected toolbar button: dark top-left,
// light bottom-right, slightly darker fill.
inline void draw_bevel_panel_pressed(float x, float y, float w, float h,
                                     Color bg) {
    draw_rect(x, y, w, h, bg);
    draw_rect(x, y, w, 2, Color{64, 64, 64, 255});
    draw_rect(x, y, 2, h, Color{64, 64, 64, 255});
    draw_rect(x, y + h - 2, w, 2, Color{255, 255, 255, 255});
    draw_rect(x + w - 2, y, 2, h, Color{255, 255, 255, 255});
}

// Two-tone shading for building roofs/facades: lighten toward white,
// darken toward black.
inline Color lighten_color(Color c, float f) {
    return Color{(uint8_t) (c.r + (255 - c.r) * f),
                 (uint8_t) (c.g + (255 - c.g) * f),
                 (uint8_t) (c.b + (255 - c.b) * f), c.a};
}

inline Color darken_color(Color c, float f) {
    return Color{(uint8_t) (c.r * (1.0f - f)), (uint8_t) (c.g * (1.0f - f)),
                 (uint8_t) (c.b * (1.0f - f)), c.a};
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
// Bright saturated '90s tycoon palette: every building type gets a distinct
// saturated hue, grass is vivid green, paths are golden sand.
inline constexpr Color TILE_DAY_COLORS[] = {
    {80, 200, 100, 255},   // Grass
    {245, 205, 120, 255},  // Path
    {150, 95, 50, 255},    // Fence
    {255, 255, 255, 255},  // Gate
    {200, 30, 230, 255},   // Stage
    {235, 190, 255, 255},  // StageFloor
    {0, 210, 200, 255},    // Bathroom
    {255, 80, 160, 255},   // Food
    {255, 45, 45, 255},    // MedTent
    {255, 145, 0, 255},    // Bar
    {50, 90, 255, 255},    // SecurityPost
    {0, 180, 255, 255},    // WaterStation
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
    {120, 80, 20, 255},   // Bar
    {70, 80, 130, 255},   // SecurityPost
    {40, 90, 130, 255},   // WaterStation
};

inline Color tile_day_color(TileType type) {
    return TILE_DAY_COLORS[static_cast<int>(type)];
}

inline Color tile_color(TileType type, float night_t) {
    int idx = static_cast<int>(type);
    return lerp_color(TILE_DAY_COLORS[idx], TILE_NIGHT_COLORS[idx], night_t);
}

// ── Bottom news ticker ──
// Queue component for the scrolling ticker strip. Game events feed it via
// ticker_poll_events() (defined in render_ui.cpp), which watches the
// singletons each render frame — no engine/update-system changes.
// Breaking items pre-empt whatever is scrolling so the newest event is
// visible (and expect_text-assertable) within a frame; the interrupted
// item goes back to the front of the queue. When the queue is empty the
// ticker rotates through satirical flavor lines.
inline constexpr const char* NEWS_TICKER_FLAVOR[] = {
    "Local man insists the porta-potties are 'part of the experience'.",
    "Security confiscates 400 glow sticks. Nobody knows why they brought "
    "them.",
    "The mayor denies the festival is a front for selling overpriced water.",
    "Scientists confirm: bass drops cause 12% more questionable life "
    "choices.",
    "Food stall voted 'Most Likely To Be Condemned By Tuesday'.",
    "One attendee has been dancing non-stop since Tuesday. It is Friday.",
    "The parking lot remains undefeated. Cars fear it.",
    "DJ promises 'just one more song' for the ninth time.",
};
inline constexpr size_t NEWS_TICKER_FLAVOR_COUNT =
    sizeof(NEWS_TICKER_FLAVOR) / sizeof(NEWS_TICKER_FLAVOR[0]);

struct NewsTicker {
    std::deque<std::string> queue;
    std::string current;
    float scroll_px = 0.f;
    size_t flavor_idx = 0;

    // Last-seen game state, for edge-detecting events. A drop means the
    // game was reset: resync silently instead of re-reporting old events.
    bool seeded = false;
    int last_deaths = 0;
    int last_injuries = 0;
    int last_inspections = 0;
    int last_milestone_idx = 0;
    int last_day = 1;
    bool last_game_over = false;
    int last_staff_quits = 0;

    void push_event(const std::string& text) {
        if (text.empty()) return;
        if (!current.empty()) queue.push_front(current);
        if (queue.size() > 16) queue.resize(16);
        current = text;
        scroll_px = 0.f;
    }
};

inline NewsTicker& news_ticker() {
    static NewsTicker ticker;
    return ticker;
}

// Edge-detects game events (inspection verdicts, milestones, deaths,
// injuries, day rollovers, shutdown) and feeds them to the ticker queue.
void ticker_poll_events();
