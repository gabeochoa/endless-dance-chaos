// AFTER_HOURS_REPLACE_LOGGING and log.h must come first
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "afterhours/src/core/entity_query.h"
#include "afterhours/src/plugins/e2e_testing/visible_text.h"
#include "components.h"
#include "entity_makers.h"
#include "game.h"
#include "save_system.h"
#include "systems.h"

// Render texture from main.cpp for MCP screenshots
extern raylib::RenderTexture2D g_render_texture;

// Global font - loaded once
static raylib::Font g_font = {0};
static bool g_font_loaded = false;
static constexpr float FONT_SPACING = 1.5f;

static raylib::Font& get_font() {
    if (!g_font_loaded) {
        g_font =
            raylib::LoadFontEx("resources/fonts/Exo2-Bold.ttf", 96, nullptr, 0);
        raylib::SetTextureFilter(g_font.texture,
                                 raylib::TEXTURE_FILTER_BILINEAR);
        g_font_loaded = true;
    }
    return g_font;
}

// Forward declarations for day/night system (defined below)
static float get_day_night_t();
static raylib::Color lerp_color(raylib::Color a, raylib::Color b, float t);

struct BeginRenderSystem : System<> {
    void once(float) const override {
        // Render to texture for MCP screenshot support
        raylib::BeginTextureMode(g_render_texture);
        float nt = get_day_night_t();
        raylib::Color bg = lerp_color({40, 44, 52, 255}, {10, 10, 20, 255}, nt);
        raylib::ClearBackground(bg);

        auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
        if (cam) raylib::BeginMode3D(cam->cam.camera);
    }
};

// Day/Night color palette with smoothstep transition
static float smoothstep(float edge0, float edge1, float x) {
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static raylib::Color lerp_color(raylib::Color a, raylib::Color b, float t) {
    return raylib::Color{
        (uint8_t) (a.r + (b.r - a.r) * t), (uint8_t) (a.g + (b.g - a.g) * t),
        (uint8_t) (a.b + (b.b - a.b) * t), (uint8_t) (a.a + (b.a - a.a) * t)};
}

static float get_day_night_t() {
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
    return 1.0f;  // Dead hours: night palette
}

// Shared day-palette colors for tiles, used by grid rendering and minimap.
// Indexed by TileType enum value.
static constexpr raylib::Color TILE_DAY_COLORS[] = {
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
static constexpr raylib::Color TILE_NIGHT_COLORS[] = {
    {45, 74, 62, 255},    // Grass
    {42, 42, 58, 255},    // Path
    {40, 40, 50, 255},    // Fence
    {68, 136, 170, 255},  // Gate (unchanged at night)
    {255, 230, 0, 255},   // Stage
    {60, 60, 40, 255},    // StageFloor
    {60, 100, 90, 255},   // Bathroom
    {120, 80, 80, 255},   // Food
    {120, 50, 50, 255},   // MedTent
};

static raylib::Color tile_day_color(TileType type) {
    return TILE_DAY_COLORS[static_cast<int>(type)];
}

static raylib::Color tile_color(TileType type, float night_t) {
    int idx = static_cast<int>(type);
    return lerp_color(TILE_DAY_COLORS[idx], TILE_NIGHT_COLORS[idx], night_t);
}

struct RenderGridSystem : System<> {
    void once(float) const override {
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;

        float tile_size = TILESIZE * 0.98f;
        float night_t = get_day_night_t();

        for (int z = 0; z < MAP_SIZE; z++) {
            for (int x = 0; x < MAP_SIZE; x++) {
                const Tile& tile = grid->at(x, z);
                raylib::Color color = ::tile_color(tile.type, night_t);

                raylib::DrawPlane({x * TILESIZE, 0.01f, z * TILESIZE},
                                  {tile_size, tile_size}, color);
            }
        }
    }
};

// Stage performance glow overlay with pulsing lights and spotlights
struct RenderStageGlowSystem : System<> {
    void once(float) const override {
        auto* sched = EntityHelper::get_singleton_cmp<ArtistSchedule>();
        if (!sched) return;

        float stage_cx = (STAGE_X + STAGE_SIZE / 2.0f) * TILESIZE;
        float stage_cz = (STAGE_Z + STAGE_SIZE / 2.0f) * TILESIZE;

        // Announcing state: gentle pulse
        if (sched->stage_state == StageState::Announcing) {
            float t = static_cast<float>(raylib::GetTime());
            float pulse = (std::sin(t * 3.0f) + 1.0f) * 0.5f;
            auto alpha = static_cast<unsigned char>(40 + pulse * 40);
            raylib::Color glow = {255, 200, 50, alpha};
            float ts = TILESIZE * 0.98f;
            for (int z = STAGE_Z; z < STAGE_Z + STAGE_SIZE; z++)
                for (int x = STAGE_X; x < STAGE_X + STAGE_SIZE; x++)
                    raylib::DrawPlane({x * TILESIZE, 0.06f, z * TILESIZE},
                                      {ts, ts}, glow);
            return;
        }

        if (sched->stage_state != StageState::Performing) return;

        float t = static_cast<float>(raylib::GetTime());
        float beat_phase = std::fmod(t * (128.0f / 60.0f), 1.0f);
        float pulse = std::pow(std::max(0.0f, 1.0f - beat_phase * 4.0f), 2.0f);

        // Stage floor glow - pulsing with beat
        auto glow_alpha = static_cast<unsigned char>(60 + pulse * 100);
        raylib::Color glow = {255, 180, 0, glow_alpha};
        float ts = TILESIZE * 0.98f;
        for (int z = STAGE_Z; z < STAGE_Z + STAGE_SIZE; z++)
            for (int x = STAGE_X; x < STAGE_X + STAGE_SIZE; x++)
                raylib::DrawPlane({x * TILESIZE, 0.06f, z * TILESIZE}, {ts, ts},
                                  glow);

        // Spotlight beams from stage corners
        float beam_h = 2.5f + pulse * 0.5f;
        float beam_r = 0.06f;
        raylib::Color beam_colors[] = {
            {255, 50, 50, 120},   // red
            {50, 50, 255, 120},   // blue
            {50, 255, 50, 120},   // green
            {255, 255, 50, 120},  // yellow
        };
        float corners[][2] = {
            {STAGE_X * TILESIZE, STAGE_Z * TILESIZE},
            {(STAGE_X + STAGE_SIZE) * TILESIZE, STAGE_Z * TILESIZE},
            {STAGE_X * TILESIZE, (STAGE_Z + STAGE_SIZE) * TILESIZE},
            {(STAGE_X + STAGE_SIZE) * TILESIZE,
             (STAGE_Z + STAGE_SIZE) * TILESIZE},
        };
        for (int i = 0; i < 4; i++) {
            float angle = t * 1.5f + i * 1.57f;
            float sway_x = std::sin(angle) * 0.3f;
            float sway_z = std::cos(angle * 0.7f) * 0.3f;
            raylib::DrawCylinder({corners[i][0], 0.0f, corners[i][1]}, beam_r,
                                 beam_r * 0.3f, beam_h, 4, beam_colors[i]);
            // Sway tip indicator
            raylib::DrawSphere(
                {corners[i][0] + sway_x, beam_h, corners[i][1] + sway_z}, 0.08f,
                beam_colors[i]);
        }

        // Center spotlight cone
        auto spot_alpha = static_cast<unsigned char>(80 + pulse * 80);
        raylib::DrawCylinder({stage_cx, 0.0f, stage_cz}, 0.1f,
                             0.6f + pulse * 0.2f, 2.0f + pulse * 0.5f, 6,
                             {255, 255, 200, spot_alpha});
    }
};

// Render agents as small person-shaped primitives in 3D space
struct RenderAgentsSystem : System<Agent, Transform> {
    // Festival attendee color palette: diverse, vibrant outfits
    static constexpr raylib::Color PALETTE[] = {
        {212, 165, 116, 255},  // warm tan
        {180, 120, 90, 255},   // brown
        {240, 200, 160, 255},  // light peach
        {100, 80, 60, 255},    // dark brown
        {255, 180, 200, 255},  // pink top
        {100, 200, 255, 255},  // blue top
        {200, 255, 100, 255},  // lime top
        {255, 220, 100, 255},  // yellow top
    };

    void for_each_with(Entity& e, Agent& agent, Transform& tf, float) override {
        if (!e.is_missing<BeingServiced>()) return;

        float wx = tf.position.x;
        float wz = tf.position.y;
        raylib::Color col = PALETTE[agent.color_idx % 8];

        // Bob animation when watching stage
        float bob_y = 0.0f;
        if (!e.is_missing<WatchingStage>()) {
            auto& ws = e.get<WatchingStage>();
            bob_y = std::sin(ws.watch_timer * 6.0f) * 0.03f;
        }

        // Low HP: tint red
        if (!e.is_missing<AgentHealth>()) {
            float hp = e.get<AgentHealth>().hp;
            if (hp < 0.5f) {
                float t = hp / 0.5f;
                col.r = static_cast<unsigned char>(col.r * t + 255 * (1.f - t));
                col.g = static_cast<unsigned char>(col.g * t);
                col.b = static_cast<unsigned char>(col.b * t);
            }
        }

        raylib::DrawCube({wx, 0.2f + bob_y, wz}, 0.18f, 0.4f, 0.18f, col);
    }
};

// Render path-drawing preview overlays on the grid
struct RenderBuildPreviewSystem : System<> {
    // Semi-transparent green for valid new path tiles
    static constexpr raylib::Color PREVIEW_VALID = {100, 220, 130, 100};
    // Semi-transparent gray for tiles already path
    static constexpr raylib::Color PREVIEW_EXISTING = {180, 180, 180, 80};
    // Hover cursor (white outline plane)
    static constexpr raylib::Color HOVER_NORMAL = {255, 255, 255, 120};
    // Red hover in demolish mode
    static constexpr raylib::Color HOVER_DEMOLISH = {255, 60, 60, 140};

    void once(float) const override {
        auto* pds = EntityHelper::get_singleton_cmp<PathDrawState>();
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!pds || !grid || !pds->hover_valid) return;

        float tile_size = TILESIZE * 0.98f;
        float preview_y = 0.03f;  // Slightly above tiles

        // Draw rectangle preview while drawing
        if (pds->is_drawing) {
            int min_x, min_z, max_x, max_z;
            pds->get_rect(min_x, min_z, max_x, max_z);

            for (int z = min_z; z <= max_z; z++) {
                for (int x = min_x; x <= max_x; x++) {
                    if (!grid->in_bounds(x, z)) continue;
                    bool already_path = grid->at(x, z).type == TileType::Path;
                    raylib::Color color =
                        already_path ? PREVIEW_EXISTING : PREVIEW_VALID;
                    raylib::DrawPlane({x * TILESIZE, preview_y, z * TILESIZE},
                                      {tile_size, tile_size}, color);
                }
            }
        }

        // Draw hover cursor
        raylib::Color cursor_color =
            pds->demolish_mode ? HOVER_DEMOLISH : HOVER_NORMAL;
        raylib::DrawPlane(
            {pds->hover_x * TILESIZE, preview_y, pds->hover_z * TILESIZE},
            {tile_size, tile_size}, cursor_color);
    }
};

// Density heat map overlay (3D planes above tiles)
struct RenderDensityOverlaySystem : System<> {
    static raylib::Color get_density_color(float density_ratio) {
        if (density_ratio < 0.50f) {
            // Transparent -> Yellow (0% - 50%)
            float t = density_ratio / 0.50f;
            return raylib::Color{255, 255, 0,
                                 static_cast<unsigned char>(t * 180)};
        } else if (density_ratio < 0.75f) {
            // Yellow -> Orange (50% - 75%)
            float t = (density_ratio - 0.50f) / 0.25f;
            return raylib::Color{255, static_cast<unsigned char>(255 - t * 90),
                                 0, 180};
        } else if (density_ratio < 0.90f) {
            // Orange -> Red (75% - 90%)
            float t = (density_ratio - 0.75f) / 0.15f;
            return raylib::Color{255, static_cast<unsigned char>(165 - t * 165),
                                 0, 200};
        } else {
            // Red -> Black (90% - 100%)
            float t = std::min((density_ratio - 0.90f) / 0.10f, 1.0f);
            return raylib::Color{static_cast<unsigned char>(255 - t * 255), 0,
                                 0, 220};
        }
    }

    void once(float) const override {
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        if (!gs || !gs->show_data_layer) return;

        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;

        float tile_size = TILESIZE * 0.98f;
        float overlay_y = 0.05f;  // Above tiles and build preview

        for (int z = 0; z < MAP_SIZE; z++) {
            for (int x = 0; x < MAP_SIZE; x++) {
                const Tile& tile = grid->at(x, z);
                if (tile.agent_count <= 0) continue;

                float density =
                    tile.agent_count / static_cast<float>(MAX_AGENTS_PER_TILE);
                raylib::Color color = get_density_color(density);

                raylib::DrawPlane({x * TILESIZE, overlay_y, z * TILESIZE},
                                  {tile_size, tile_size}, color);
            }
        }
    }
};

// Render death particles as small 3D cubes
struct RenderParticlesSystem : System<Particle, Transform> {
    void for_each_with(Entity&, Particle& p, Transform& tf, float) override {
        float wx = tf.position.x;
        float wz = tf.position.y;
        float s = p.size * 0.02f;  // Scale down for 3D world

        // Fade height: particles rise slightly as they fade
        float life_t = 1.0f - (p.lifetime / p.max_lifetime);
        float y = 0.1f + life_t * 0.5f;

        raylib::DrawCube({wx, y, wz}, s, s, s, p.color);
    }
};

struct EndMode3DSystem : System<> {
    void once(float) const override {
        auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
        if (cam) raylib::EndMode3D();
    }
};

// Update hover grid position from mouse (runs in render phase after EndMode3D
// because GetWorldToScreen needs internal raylib state from the 3D pass)
struct HoverTrackingSystem : System<> {
    // Check if mouse is over any UI panel (build bar, top bar, sidebar, debug)
    static bool mouse_over_ui(float mx, float my) {
        // Top bar (44px)
        if (my < 44) return true;
        // Build bar (54px at bottom)
        if (my > DEFAULT_SCREEN_HEIGHT - 54) return true;
        // Timeline sidebar (150px on right)
        if (mx > DEFAULT_SCREEN_WIDTH - 150) return true;
        // Debug panel (if visible)
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        if (gs && gs->show_debug) {
            float pw = 300, ph = 240;
            float px = 10;
            float py = DEFAULT_SCREEN_HEIGHT - 54 - ph - 10;
            if (mx >= px && mx <= px + pw && my >= py && my <= py + ph)
                return true;
        }
        return false;
    }

    void once(float) const override {
        auto* pds = EntityHelper::get_singleton_cmp<PathDrawState>();
        auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!pds || !cam || !grid) return;

        // E2E click_grid sets hover_lock_frames to prevent us from
        // overwriting its injected hover values until PathBuildSystem
        // has a chance to see them.
        if (pds->hover_lock_frames > 0) {
            --pds->hover_lock_frames;
            return;
        }

        auto mouse = input::get_mouse_position();

        // Block grid interaction when mouse is over UI
        if (mouse_over_ui(mouse.x, mouse.y)) {
            pds->hover_valid = false;
            return;
        }

        auto result = cam->cam.screen_to_grid(mouse.x, mouse.y);

        if (result.has_value()) {
            auto [gx, gz] = result.value();
            pds->hover_x = gx;
            pds->hover_z = gz;
            pds->hover_valid = grid->in_bounds(gx, gz);
        } else {
            pds->hover_valid = false;
        }
    }
};

// Render facility labels as 2D text projected from 3D positions
struct RenderFacilityLabelsSystem : System<> {
    struct FacilityLabel {
        const char* text;
        float world_x, world_z;
        raylib::Color color;
    };

    void once(float) const override {
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
        if (!grid || !cam) return;

        // Collect unique facility positions and labels
        std::vector<FacilityLabel> labels;

        // Stage
        float scx = (STAGE_X + STAGE_SIZE / 2.0f) * TILESIZE;
        float scz = (STAGE_Z + STAGE_SIZE / 2.0f) * TILESIZE;
        labels.push_back({"STAGE", scx, scz, {255, 217, 61, 255}});

        // Scan grid for facility clusters (take top-left of each)
        bool bathroom_found[MAP_SIZE * MAP_SIZE] = {};
        bool food_found[MAP_SIZE * MAP_SIZE] = {};
        bool med_found[MAP_SIZE * MAP_SIZE] = {};

        for (int z = 0; z < MAP_SIZE; z++) {
            for (int x = 0; x < MAP_SIZE; x++) {
                TileType t = grid->at(x, z).type;
                int idx = z * MAP_SIZE + x;
                if (t == TileType::Bathroom && !bathroom_found[idx]) {
                    labels.push_back({"WC",
                                      (x + 1.0f) * TILESIZE,
                                      (z + 1.0f) * TILESIZE,
                                      {126, 207, 192, 255}});
                    for (int dz = 0; dz < 2; dz++)
                        for (int dx = 0; dx < 2; dx++)
                            if (grid->in_bounds(x + dx, z + dz))
                                bathroom_found[(z + dz) * MAP_SIZE + x + dx] =
                                    true;
                } else if (t == TileType::Food && !food_found[idx]) {
                    labels.push_back({"FOOD",
                                      (x + 1.0f) * TILESIZE,
                                      (z + 1.0f) * TILESIZE,
                                      {244, 164, 164, 255}});
                    for (int dz = 0; dz < 2; dz++)
                        for (int dx = 0; dx < 2; dx++)
                            if (grid->in_bounds(x + dx, z + dz))
                                food_found[(z + dz) * MAP_SIZE + x + dx] = true;
                } else if (t == TileType::MedTent && !med_found[idx]) {
                    labels.push_back({"MED",
                                      (x + 1.0f) * TILESIZE,
                                      (z + 1.0f) * TILESIZE,
                                      {255, 100, 100, 255}});
                    for (int dz = 0; dz < 2; dz++)
                        for (int dx = 0; dx < 2; dx++)
                            if (grid->in_bounds(x + dx, z + dz))
                                med_found[(z + dz) * MAP_SIZE + x + dx] = true;
                }
            }
        }

        // Gate labels: find first gate tile, place label centered on the 1x2
        // gate
        {
            bool gate_found = false;
            for (int z = 0; z < MAP_SIZE && !gate_found; z++) {
                for (int x = 0; x < MAP_SIZE && !gate_found; x++) {
                    if (grid->at(x, z).type == TileType::Gate) {
                        // Center label on the gate pair (1x2)
                        labels.push_back({"GATE",
                                          x * TILESIZE,
                                          (z + 0.5f) * TILESIZE,
                                          {68, 136, 170, 255}});
                        gate_found = true;
                    }
                }
            }
        }

        // Project to screen and draw
        for (auto& lbl : labels) {
            raylib::Vector2 screen = raylib::GetWorldToScreen(
                {lbl.world_x, 0.6f, lbl.world_z}, cam->cam.camera);

            if (screen.x < -50 || screen.x > DEFAULT_SCREEN_WIDTH + 50 ||
                screen.y < -50 || screen.y > DEFAULT_SCREEN_HEIGHT + 50)
                continue;

            float font_size = 13;
            raylib::Vector2 m = raylib::MeasureTextEx(get_font(), lbl.text,
                                                      font_size, FONT_SPACING);
            float tx = screen.x - m.x / 2.f;
            float ty = screen.y - m.y / 2.f;

            raylib::DrawRectangle((int) (tx - 3), (int) (ty - 2),
                                  (int) (m.x + 6), (int) (m.y + 4),
                                  raylib::Color{0, 0, 0, 160});
            raylib::DrawTextEx(get_font(), lbl.text, {tx, ty}, font_size,
                               FONT_SPACING, lbl.color);
        }
    }
};

struct RenderUISystem : System<> {
    static void draw_text(const std::string& text, float x, float y, float size,
                          raylib::Color color) {
        raylib::DrawTextEx(get_font(), text.c_str(), {x, y}, size, FONT_SPACING,
                           color);
    }

    static raylib::Vector2 measure_text(const std::string& text, float size) {
        return raylib::MeasureTextEx(get_font(), text.c_str(), size,
                                     FONT_SPACING);
    }

    static void draw_text_bg(const std::string& text, float x, float y,
                             float size, raylib::Color color) {
        auto m = measure_text(text, size);
        raylib::DrawRectangle((int) x - 6, (int) y - 3, (int) m.x + 12,
                              (int) m.y + 6, raylib::Color{0, 0, 0, 240});
        draw_text(text, x, y, size, color);
    }

    static void draw_text_centered(const std::string& text, float y, float size,
                                   raylib::Color color) {
        auto m = measure_text(text, size);
        float x = (DEFAULT_SCREEN_WIDTH - m.x) / 2.f;
        draw_text(text, x, y, size, color);
    }

    // Build tool colors, labels, and full names
    struct ToolInfo {
        const char* label;
        raylib::Color color;
        const char* full_name;
    };
    static constexpr ToolInfo TOOL_INFO[] = {
        {"P", {184, 168, 138, 255}, "Path"},
        {"F", {136, 136, 136, 255}, "Fence"},
        {"G", {68, 136, 170, 255}, "Gate"},
        {"S", {255, 217, 61, 255}, "Stage"},
        {"B", {126, 207, 192, 255}, "Bathroom"},
        {"Fd", {244, 164, 164, 255}, "Food Stall"},
        {"M", {255, 100, 100, 255}, "Med Tent"},
        {"X", {255, 68, 68, 255}, "Demolish"},
    };
    static constexpr int TOOL_COUNT = 8;

    void once(float) const override {
        auto& vtr = afterhours::testing::VisibleTextRegistry::instance();
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        auto* clock = EntityHelper::get_singleton_cmp<GameClock>();

        // === TOP BAR (44px) ===
        raylib::DrawRectangle(0, 0, DEFAULT_SCREEN_WIDTH, 44,
                              raylib::Color{0, 0, 0, 180});
        float bar_x = 12;
        if (clock) {
            std::string time_str = clock->format_time();
            draw_text(time_str, bar_x, 10, 22, raylib::WHITE);
            vtr.register_text(time_str);
            bar_x += 90;
            std::string phase_str = GameClock::phase_name(clock->get_phase());
            draw_text(phase_str, bar_x, 11, 20,
                      raylib::Color{255, 220, 100, 255});
            vtr.register_text(phase_str);
            bar_x += 130;
            if (clock->speed == GameSpeed::Paused) {
                draw_text("PAUSED", bar_x, 11, 20,
                          raylib::Color{255, 100, 100, 255});
                vtr.register_text("PAUSED");
                bar_x += 90;
            }
        }
        if (gs) {
            std::string death_text =
                fmt::format("Deaths: {}/{}", gs->death_count, gs->max_deaths);
            raylib::Color dc = gs->death_count >= 7
                                   ? raylib::Color{255, 80, 80, 255}
                                   : raylib::WHITE;
            draw_text(death_text, bar_x, 11, 20, dc);
            vtr.register_text(death_text);
            bar_x += 170;
            int agent_count =
                (int) EntityQuery().whereHasComponent<Agent>().gen_count();
            std::string att_text = fmt::format("Attendees: {}", agent_count);
            draw_text(att_text, bar_x, 11, 20, raylib::WHITE);
            vtr.register_text(att_text);
            bar_x += measure_text(att_text, 20).x + 20;
        }
        // Day counter
        auto* diff = EntityHelper::get_singleton_cmp<DifficultyState>();
        if (diff) {
            std::string day_text = fmt::format("Day {}", diff->day_number);
            draw_text(day_text, bar_x, 11, 20,
                      raylib::Color{180, 220, 255, 255});
            vtr.register_text(day_text);
            bar_x += 90;
        }

        // Active event indicator
        {
            auto events = EntityQuery().whereHasComponent<ActiveEvent>().gen();
            for (Entity& ev_e : events) {
                auto& ev = ev_e.get<ActiveEvent>();
                float remain = ev.duration - ev.elapsed;
                std::string ev_text =
                    fmt::format("{} ({:.0f}s)", ev.description, remain);
                draw_text(ev_text, bar_x, 11, 16,
                          raylib::Color{255, 200, 80, 255});
                vtr.register_text(ev_text);
                bar_x += measure_text(ev_text, 16).x + 12;
            }
        }

        // FPS (top-right in top bar)
        int fps = raylib::GetFPS();
        std::string fps_text = fmt::format("FPS: {}", fps);
        auto fps_measure = measure_text(fps_text, 18);
        draw_text(fps_text, DEFAULT_SCREEN_WIDTH - 150 - fps_measure.x - 10, 12,
                  18,
                  fps >= 55 ? raylib::Color{100, 255, 100, 255}
                            : raylib::Color{255, 80, 80, 255});

        // === BUILD BAR (54px at bottom) ===
        float build_bar_y = DEFAULT_SCREEN_HEIGHT - 54.f;
        raylib::DrawRectangle(0, (int) build_bar_y, DEFAULT_SCREEN_WIDTH, 54,
                              raylib::Color{0, 0, 0, 180});
        auto* bs = EntityHelper::get_singleton_cmp<BuilderState>();
        auto mouse = input::get_mouse_position();
        bool mouse_clicked =
            input::is_mouse_button_pressed(raylib::MOUSE_BUTTON_LEFT);
        if (bs) {
            float icon_size = 36.f;
            float gap = 6.f;
            float total_w = TOOL_COUNT * icon_size + (TOOL_COUNT - 1) * gap;
            float start_x = (DEFAULT_SCREEN_WIDTH - 150 - total_w) / 2.f;
            for (int i = 0; i < TOOL_COUNT; i++) {
                float ix = start_x + i * (icon_size + gap);
                float iy = build_bar_y + (54 - icon_size) / 2.f;
                bool selected = (static_cast<int>(bs->tool) == i);
                float s = selected ? icon_size * 1.15f : icon_size;
                float ox = ix - (s - icon_size) / 2.f;
                float oy = iy - (s - icon_size) / 2.f;

                // Click to select tool
                if (mouse_clicked && mouse.x >= ox && mouse.x <= ox + s &&
                    mouse.y >= oy && mouse.y <= oy + s) {
                    bs->tool = static_cast<BuildTool>(i);
                    selected = true;
                }

                raylib::Color bg = TOOL_INFO[i].color;
                if (selected) {
                    bg.r = (uint8_t) std::min(255, bg.r + 40);
                    bg.g = (uint8_t) std::min(255, bg.g + 40);
                    bg.b = (uint8_t) std::min(255, bg.b + 40);
                }
                raylib::DrawRectangle((int) ox, (int) oy, (int) s, (int) s, bg);
                if (selected) {
                    raylib::DrawRectangleLines((int) ox, (int) oy, (int) s,
                                               (int) s, raylib::WHITE);
                }
                auto label_m = measure_text(TOOL_INFO[i].label, 16);
                float lx = ox + (s - label_m.x) / 2.f;
                float ly = oy + (s - label_m.y) / 2.f;
                draw_text(TOOL_INFO[i].label, lx, ly, 16, raylib::WHITE);

                // Hover tooltip
                if (mouse.x >= ox && mouse.x <= ox + s && mouse.y >= oy &&
                    mouse.y <= oy + s) {
                    auto tip_m = measure_text(TOOL_INFO[i].full_name, 16);
                    float tip_x = ox + (s - tip_m.x) / 2.f;
                    float tip_y = oy - tip_m.y - 8.f;
                    raylib::DrawRectangle((int) (tip_x - 4), (int) (tip_y - 2),
                                          (int) (tip_m.x + 8),
                                          (int) (tip_m.y + 4),
                                          raylib::Color{20, 20, 30, 220});
                    draw_text(TOOL_INFO[i].full_name, tip_x, tip_y, 16,
                              raylib::WHITE);
                }
            }
        }

        // Toast messages (below top bar)
        {
            auto toasts = EntityQuery().whereHasComponent<ToastMessage>().gen();
            float toast_y = 50.f;
            for (Entity& te : toasts) {
                auto& toast = te.get<ToastMessage>();
                float alpha = 1.0f;
                if (toast.elapsed > toast.lifetime - toast.fade_duration) {
                    alpha =
                        (toast.lifetime - toast.elapsed) / toast.fade_duration;
                }
                unsigned char a = static_cast<unsigned char>(alpha * 255);
                auto tm = measure_text(toast.text, 20);
                float tx = (DEFAULT_SCREEN_WIDTH - tm.x) / 2.f;
                raylib::DrawRectangle((int) (tx - 8), (int) (toast_y - 4),
                                      (int) (tm.x + 16), (int) (tm.y + 8),
                                      raylib::Color{30, 120, 60, a});
                raylib::DrawTextEx(get_font(), toast.text.c_str(),
                                   {tx, toast_y}, 20, FONT_SPACING,
                                   raylib::Color{255, 255, 255, a});
                vtr.register_text(toast.text);
                toast_y += tm.y + 16;
            }
        }

        // Compass indicator (top-right area, near sidebar)
        {
            auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
            if (cam) {
                float cx = DEFAULT_SCREEN_WIDTH - 150 - 30;
                float cy = 55;
                raylib::DrawCircle((int) cx, (int) cy, 16,
                                   raylib::Color{0, 0, 0, 120});
                // Approximate rotation from camera position relative to target
                float cam_dx =
                    cam->cam.camera.position.x - cam->cam.camera.target.x;
                float cam_dz =
                    cam->cam.camera.position.z - cam->cam.camera.target.z;
                float angle = std::atan2(cam_dz, cam_dx);
                float nx = cx + std::cos(angle) * 12;
                float ny = cy + std::sin(angle) * 12;
                draw_text("N", nx - 6, ny - 8, 14,
                          raylib::Color{255, 100, 100, 255});
            }
        }

        // Grid hover info (above build bar)
        auto* pds = EntityHelper::get_singleton_cmp<PathDrawState>();
        if (pds && pds->hover_valid) {
            auto* grid = EntityHelper::get_singleton_cmp<Grid>();
            if (grid && grid->in_bounds(pds->hover_x, pds->hover_z)) {
                const auto& tile = grid->at(pds->hover_x, pds->hover_z);
                std::string hover_text =
                    fmt::format("({}, {})  Agents: {}", pds->hover_x,
                                pds->hover_z, tile.agent_count);
                draw_text_bg(hover_text, 10, build_bar_y - 30, 18,
                             raylib::Color{200, 200, 200, 255});
            }
        }
        if (gs && gs->show_data_layer) {
            draw_text_bg("[TAB] Density Overlay", 10, build_bar_y - 56, 18,
                         raylib::Color{255, 255, 100, 255});
        }

        // Game over screen rendered by RenderGameOverSystem (after sidebar)
    }
};

// Minimap RenderTexture management
static raylib::RenderTexture2D g_minimap_texture = {0};
static bool g_minimap_initialized = false;

static constexpr int MINIMAP_SIZE = 150;
static constexpr float MINIMAP_SCALE = 150.0f / 52.0f;

// Minimap uses day-palette colors (shared via tile_day_color)

// Timeline sidebar showing artist schedule
struct RenderTimelineSidebarSystem : System<> {
    void once(float) const override {
        auto* sched = EntityHelper::get_singleton_cmp<ArtistSchedule>();
        auto* clock = EntityHelper::get_singleton_cmp<GameClock>();
        if (!sched || !clock) return;

        float sidebar_w = 150.f;
        float sidebar_x = DEFAULT_SCREEN_WIDTH - sidebar_w;
        float sidebar_y = 0.f;
        float sidebar_h = static_cast<float>(DEFAULT_SCREEN_HEIGHT);

        // Background
        raylib::DrawRectangle((int) sidebar_x, (int) sidebar_y, (int) sidebar_w,
                              (int) sidebar_h, raylib::Color{15, 15, 25, 200});

        // Title
        raylib::DrawTextEx(get_font(), "LINEUP", {sidebar_x + 10, 8}, 18,
                           FONT_SPACING, raylib::Color{255, 220, 100, 255});
        auto& vtr = afterhours::testing::VisibleTextRegistry::instance();
        vtr.register_text("LINEUP");

        // Clip region for sidebar content (below title bar, above minimap)
        float content_top = 30.f;
        float content_bot = DEFAULT_SCREEN_HEIGHT - 150.f;
        raylib::BeginScissorMode((int) sidebar_x, (int) content_top,
                                 (int) sidebar_w,
                                 (int) (content_bot - content_top));

        // NOW marker at ~20% from top
        float now_y = sidebar_y + sidebar_h * 0.2f;
        raylib::DrawLine((int) sidebar_x, (int) now_y,
                         (int) (sidebar_x + sidebar_w), (int) now_y,
                         raylib::Color{255, 100, 100, 255});
        raylib::DrawTextEx(get_font(), "NOW", {sidebar_x + 6, now_y - 18}, 16,
                           FONT_SPACING, raylib::Color{255, 100, 100, 255});

        float now_minutes = clock->game_time_minutes;
        float px_per_minute = 2.4f;

        // Draw artist blocks
        for (auto& a : sched->schedule) {
            float minutes_from_now = a.start_time_minutes - now_minutes;
            float block_y = now_y + minutes_from_now * px_per_minute;
            float block_h = std::max(a.duration_minutes * px_per_minute, 42.f);

            // Skip if off screen
            if (block_y + block_h < content_top || block_y > content_bot)
                continue;

            // Background color
            raylib::Color bg = a.performing ? raylib::Color{255, 217, 61, 80}
                                            : raylib::Color{40, 40, 60, 180};
            raylib::DrawRectangle((int) (sidebar_x + 4), (int) block_y,
                                  (int) (sidebar_w - 8), (int) block_h, bg);
            raylib::DrawRectangleLines((int) (sidebar_x + 4), (int) block_y,
                                       (int) (sidebar_w - 8), (int) block_h,
                                       raylib::Color{100, 100, 120, 200});

            // Artist name (larger, bold-simulated via shadow)
            std::string label =
                a.performing ? fmt::format("> {}", a.name) : a.name;
            raylib::Color name_col =
                a.performing ? raylib::Color{255, 230, 80, 255} : raylib::WHITE;
            raylib::DrawTextEx(get_font(), label.c_str(),
                               {sidebar_x + 11, block_y + 5}, 16, FONT_SPACING,
                               raylib::Color{0, 0, 0, 120});
            raylib::DrawTextEx(get_font(), label.c_str(),
                               {sidebar_x + 10, block_y + 4}, 16, FONT_SPACING,
                               name_col);

            // Time and crowd
            int h = (int) (a.start_time_minutes / 60) % 24;
            int m = (int) a.start_time_minutes % 60;
            std::string info =
                fmt::format("{:02d}:{:02d}  ~{} ppl", h, m, a.expected_crowd);
            raylib::DrawTextEx(get_font(), info.c_str(),
                               {sidebar_x + 10, block_y + 24}, 14, FONT_SPACING,
                               raylib::Color{190, 190, 210, 255});
        }

        raylib::EndScissorMode();
    }
};

// Minimap rendering at bottom of sidebar
struct RenderMinimapSystem : System<> {
    void once(float) const override {
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;

        if (!g_minimap_initialized) {
            g_minimap_texture =
                raylib::LoadRenderTexture(MINIMAP_SIZE, MINIMAP_SIZE);
            g_minimap_initialized = true;
        }

        // Render minimap to texture
        raylib::BeginTextureMode(g_minimap_texture);
        raylib::ClearBackground({152, 212, 168, 255});  // grass default

        for (int z = 0; z < MAP_SIZE; z++) {
            for (int x = 0; x < MAP_SIZE; x++) {
                const auto& tile = grid->at(x, z);
                if (tile.type == TileType::Grass) continue;
                raylib::Color c = tile_day_color(tile.type);
                float px = x * MINIMAP_SCALE;
                float py = z * MINIMAP_SCALE;
                float ps =
                    MINIMAP_SCALE + 0.5f;  // slight overlap to avoid gaps
                raylib::DrawRectangle((int) px, (int) py, (int) ps, (int) ps,
                                      c);
            }
        }

        // Camera viewport rectangle
        auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
        if (cam) {
            // Approximate visible area in grid coords
            float zoom = cam->cam.camera.position.y;
            float view_tiles = zoom * 1.5f;  // rough approximation
            // Camera target is at position.x, position.z in world space
            float cam_gx = cam->cam.camera.target.x / TILESIZE;
            float cam_gz = cam->cam.camera.target.z / TILESIZE;
            float mm_cx = cam_gx * MINIMAP_SCALE;
            float mm_cy = cam_gz * MINIMAP_SCALE;
            float mm_w = view_tiles * MINIMAP_SCALE;
            float mm_h = view_tiles * MINIMAP_SCALE * 0.6f;
            raylib::DrawRectangleLines((int) (mm_cx - mm_w / 2),
                                       (int) (mm_cy - mm_h / 2), (int) mm_w,
                                       (int) mm_h, raylib::WHITE);
        }

        raylib::EndTextureMode();

        // Re-enter main render texture after minimap detour
        raylib::BeginTextureMode(g_render_texture);

        // Draw minimap texture in the sidebar (bottom right)
        float sidebar_x = DEFAULT_SCREEN_WIDTH - 150.f;
        float minimap_y = DEFAULT_SCREEN_HEIGHT - MINIMAP_SIZE;
        raylib::DrawTextureRec(
            g_minimap_texture.texture,
            {0, 0, (float) MINIMAP_SIZE, -(float) MINIMAP_SIZE},
            {sidebar_x, minimap_y}, raylib::WHITE);

        // Border
        raylib::DrawRectangleLines((int) sidebar_x, (int) minimap_y,
                                   MINIMAP_SIZE, MINIMAP_SIZE,
                                   raylib::Color{100, 100, 120, 255});
    }
};

// Game over overlay - rendered after sidebar so it covers everything
struct RenderGameOverSystem : System<> {
    void once(float) const override {
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        if (!gs || !gs->is_game_over()) return;

        auto& vtr = afterhours::testing::VisibleTextRegistry::instance();

        raylib::DrawRectangle(0, 0, DEFAULT_SCREEN_WIDTH, DEFAULT_SCREEN_HEIGHT,
                              raylib::Color{0, 0, 0, 200});

        float pw = 460, ph = 360;
        float px = (DEFAULT_SCREEN_WIDTH - pw) / 2.f;
        float py = (DEFAULT_SCREEN_HEIGHT - ph) / 2.f;
        raylib::DrawRectangle((int) px, (int) py, (int) pw, (int) ph,
                              raylib::Color{20, 20, 30, 240});
        raylib::DrawRectangleLines((int) px, (int) py, (int) pw, (int) ph,
                                   raylib::Color{255, 80, 80, 255});

        std::string title = "FESTIVAL SHUT DOWN";
        RenderUISystem::draw_text_centered(title, py + 20, 34,
                                           raylib::Color{255, 80, 80, 255});

        int minutes = static_cast<int>(gs->time_survived) / 60;
        int seconds = static_cast<int>(gs->time_survived) % 60;
        std::string stats1 =
            fmt::format("Deaths: {}/{}", gs->death_count, gs->max_deaths);
        std::string stats2 =
            fmt::format("Agents Served: {}", gs->total_agents_served);
        std::string stats3 =
            fmt::format("Time Survived: {:02d}:{:02d}", minutes, seconds);
        std::string stats4 =
            fmt::format("Peak Attendees: {}", gs->max_attendees);

        float sy = py + 66;
        RenderUISystem::draw_text_centered(stats1, sy, 20, raylib::WHITE);
        RenderUISystem::draw_text_centered(stats2, sy + 28, 20, raylib::WHITE);
        RenderUISystem::draw_text_centered(stats3, sy + 56, 20, raylib::WHITE);
        RenderUISystem::draw_text_centered(stats4, sy + 84, 20, raylib::WHITE);

        save::MetaProgress meta;
        save::load_meta(meta);
        float my = sy + 124;
        raylib::DrawLine((int) (px + 20), (int) my, (int) (px + pw - 20),
                         (int) my, raylib::Color{100, 100, 120, 200});
        my += 8;
        RenderUISystem::draw_text_centered("--- All-Time Records ---", my, 16,
                                           raylib::Color{180, 200, 255, 255});
        my += 24;
        RenderUISystem::draw_text_centered(
            fmt::format("Best Day: {}  |  Best Served: {}", meta.best_day,
                        meta.best_agents_served),
            my, 16, raylib::Color{160, 180, 220, 255});
        my += 22;
        RenderUISystem::draw_text_centered(
            fmt::format("Peak Attendees: {}  |  Runs: {}",
                        meta.best_max_attendees, meta.total_runs),
            my, 16, raylib::Color{160, 180, 220, 255});

        RenderUISystem::draw_text_centered("Press SPACE to restart",
                                           py + ph - 40, 20,
                                           raylib::Color{180, 180, 180, 255});

        vtr.register_text(title);
        vtr.register_text("Press SPACE to restart");
    }
};

// Debug panel with sliders for tuning
struct RenderDebugPanelSystem : System<> {
    // Draw a horizontal slider, returns new value. Handles mouse interaction.
    static float draw_slider(const std::string& label, float x, float y,
                             float w, float val, float min_val, float max_val) {
        float h = 16.f;
        float knob_w = 10.f;

        // Label
        raylib::DrawTextEx(get_font(), label.c_str(), {x, y - 20}, 18,
                           FONT_SPACING, raylib::Color{200, 200, 200, 255});

        // Value text
        std::string val_text = fmt::format("{:.2f}", val);
        raylib::DrawTextEx(get_font(), val_text.c_str(), {x + w + 8, y - 2}, 18,
                           FONT_SPACING, raylib::Color{255, 255, 100, 255});

        // Track background
        raylib::DrawRectangle((int) x, (int) y, (int) w, (int) h,
                              raylib::Color{60, 60, 70, 255});

        // Filled portion
        float t = (val - min_val) / (max_val - min_val);
        raylib::DrawRectangle((int) x, (int) y, (int) (w * t), (int) h,
                              raylib::Color{80, 140, 220, 200});

        // Knob
        float knob_x = x + t * (w - knob_w);
        raylib::DrawRectangle((int) knob_x, (int) (y - 2), (int) knob_w,
                              (int) (h + 4), raylib::WHITE);

        // Mouse interaction
        auto mouse = input::get_mouse_position();
        if (input::is_mouse_button_down(raylib::MOUSE_BUTTON_LEFT)) {
            if (mouse.x >= x && mouse.x <= x + w && mouse.y >= y - 6 &&
                mouse.y <= y + h + 6) {
                float new_t = (mouse.x - x) / w;
                new_t = std::clamp(new_t, 0.f, 1.f);
                val = min_val + new_t * (max_val - min_val);
            }
        }

        return val;
    }

    void once(float) override {
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        if (!gs || !gs->show_debug) return;

        auto* ss = EntityHelper::get_singleton_cmp<SpawnState>();
        auto* clock = EntityHelper::get_singleton_cmp<GameClock>();

        // Panel (left side, above build bar)
        float pw = 300;
        float ph = 240;
        float px = 10;
        float py = DEFAULT_SCREEN_HEIGHT - 54 - ph - 10;

        raylib::DrawRectangle((int) px, (int) py, (int) pw, (int) ph,
                              raylib::Color{15, 15, 25, 230});
        raylib::DrawRectangleLines((int) px, (int) py, (int) pw, (int) ph,
                                   raylib::Color{100, 100, 120, 255});

        // Title
        raylib::DrawTextEx(get_font(), "Debug [`]", {px + 10, py + 8}, 20,
                           FONT_SPACING, raylib::Color{255, 200, 80, 255});

        float sx = px + 16;
        float sw = 200;

        // Agent speed multiplier slider
        gs->speed_multiplier = draw_slider("Agent Speed", sx, py + 55, sw,
                                           gs->speed_multiplier, 0.1f, 20.f);

        // Spawn rate slider (agents/sec, converted to/from interval)
        if (ss) {
            float old_rate = 1.f / ss->interval;
            float rate = old_rate;
            rate = draw_slider("Spawn Rate (agents/s)", sx, py + 105, sw, rate,
                               0.1f, 20.f);
            if (rate != old_rate) {
                ss->interval = 1.f / rate;
                ss->timer = 0.f;
                ss->manual_override = true;
            }
        }

        // Time speed slider (debug override, up to 20x)
        if (clock) {
            float cur_mult = clock->speed_multiplier();
            float new_mult = draw_slider("Time Speed", sx, py + 155, sw,
                                         cur_mult, 0.f, 20.f);
            if (new_mult != cur_mult) {
                clock->debug_time_mult = (new_mult < 0.05f) ? 0.f : new_mult;
            }
        }

        // Info
        int agent_count =
            (int) EntityQuery().whereHasComponent<Agent>().gen_count();
        std::string info =
            fmt::format("Agents: {}  Deaths: {}", agent_count, gs->death_count);
        raylib::DrawTextEx(get_font(), info.c_str(), {sx, py + 200}, 16,
                           FONT_SPACING, raylib::Color{160, 160, 160, 255});
    }
};

struct EndRenderSystem : System<> {
    void once(float) const override {
        // End rendering to texture
        raylib::EndTextureMode();

        // Draw the render texture to the screen (flipped vertically)
        raylib::BeginDrawing();
        raylib::ClearBackground(raylib::BLACK);
        raylib::DrawTextureRec(g_render_texture.texture,
                               {0, 0, (float) g_render_texture.texture.width,
                                -(float) g_render_texture.texture.height},
                               {0, 0}, raylib::WHITE);
        raylib::EndDrawing();
    }
};

void register_render_systems(SystemManager& sm) {
    sm.register_render_system(std::make_unique<BeginRenderSystem>());
    sm.register_render_system(std::make_unique<RenderGridSystem>());
    sm.register_render_system(std::make_unique<RenderStageGlowSystem>());
    sm.register_render_system(std::make_unique<RenderAgentsSystem>());
    sm.register_render_system(std::make_unique<RenderDensityOverlaySystem>());
    sm.register_render_system(std::make_unique<RenderParticlesSystem>());
    sm.register_render_system(std::make_unique<RenderBuildPreviewSystem>());
    sm.register_render_system(std::make_unique<EndMode3DSystem>());
    sm.register_render_system(std::make_unique<HoverTrackingSystem>());
    sm.register_render_system(std::make_unique<RenderFacilityLabelsSystem>());
    sm.register_render_system(std::make_unique<RenderUISystem>());
    sm.register_render_system(std::make_unique<RenderTimelineSidebarSystem>());
    sm.register_render_system(std::make_unique<RenderMinimapSystem>());
    sm.register_render_system(std::make_unique<RenderGameOverSystem>());
    sm.register_render_system(std::make_unique<RenderDebugPanelSystem>());
    sm.register_render_system(std::make_unique<EndRenderSystem>());
}
