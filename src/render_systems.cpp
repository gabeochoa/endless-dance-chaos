// AFTER_HOURS_REPLACE_LOGGING and log.h must come first
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "afterhours/src/core/entity_query.h"
#include "afterhours/src/plugins/e2e_testing/visible_text.h"
#include "components.h"
#include "entity_makers.h"
#include "game.h"
#include "systems.h"

// Render texture from main.cpp for MCP screenshots
extern raylib::RenderTexture2D g_render_texture;

// Global font - loaded once
static raylib::Font g_font = {0};
static bool g_font_loaded = false;

static raylib::Font& get_font() {
    if (!g_font_loaded) {
        // Load at high resolution for crisp downscaling
        g_font = raylib::LoadFontEx(
            "resources/fonts/Fredoka-VariableFont_wdth,wght.ttf", 128, nullptr,
            0);
        raylib::SetTextureFilter(g_font.texture,
                                 raylib::TEXTURE_FILTER_TRILINEAR);
        g_font_loaded = true;
    }
    return g_font;
}

struct BeginRenderSystem : System<> {
    void once(float) const override {
        // Render to texture for MCP screenshot support
        raylib::BeginTextureMode(g_render_texture);
        raylib::ClearBackground(raylib::Color{40, 44, 52, 255});

        auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
        if (cam) raylib::BeginMode3D(cam->cam.camera);
    }
};

struct RenderGridSystem : System<> {
    static constexpr raylib::Color GRASS_COLOR = {152, 212, 168, 255};
    static constexpr raylib::Color PATH_COLOR = {232, 221, 212, 255};
    static constexpr raylib::Color FENCE_COLOR = {85, 85, 85, 255};   // #555555
    static constexpr raylib::Color GATE_COLOR = {68, 136, 170, 255};  // #4488AA
    static constexpr raylib::Color STAGE_COLOR = {255, 217, 61,
                                                  255};  // #FFD93D warm yellow
    static constexpr raylib::Color STAGE_FLOOR_COLOR = {
        255, 235, 150, 255};  // lighter warm yellow
    static constexpr raylib::Color BATHROOM_COLOR = {126, 207, 192,
                                                     255};  // #7ECFC0 cyan
    static constexpr raylib::Color FOOD_COLOR = {244, 164, 164,
                                                 255};  // #F4A4A4 coral

    static raylib::Color tile_color(TileType type) {
        switch (type) {
            case TileType::Grass:
                return GRASS_COLOR;
            case TileType::Path:
                return PATH_COLOR;
            case TileType::Fence:
                return FENCE_COLOR;
            case TileType::Gate:
                return GATE_COLOR;
            case TileType::Stage:
                return STAGE_COLOR;
            case TileType::StageFloor:
                return STAGE_FLOOR_COLOR;
            case TileType::Bathroom:
                return BATHROOM_COLOR;
            case TileType::Food:
                return FOOD_COLOR;
        }
        return GRASS_COLOR;
    }

    void once(float) const override {
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;

        float tile_size = TILESIZE * 0.98f;

        for (int z = 0; z < MAP_SIZE; z++) {
            for (int x = 0; x < MAP_SIZE; x++) {
                const Tile& tile = grid->at(x, z);
                raylib::Color color = tile_color(tile.type);

                raylib::DrawPlane({x * TILESIZE, 0.01f, z * TILESIZE},
                                  {tile_size, tile_size}, color);
            }
        }
    }
};

// Stage performance glow overlay
struct RenderStageGlowSystem : System<> {
    void once(float) const override {
        auto* sched = EntityHelper::get_singleton_cmp<ArtistSchedule>();
        if (!sched || sched->stage_state != StageState::Performing) return;

        float tile_size = TILESIZE * 0.98f;
        raylib::Color glow = {255, 200, 0, 80};

        for (int z = STAGE_Z; z < STAGE_Z + STAGE_SIZE; z++) {
            for (int x = STAGE_X; x < STAGE_X + STAGE_SIZE; x++) {
                raylib::DrawPlane({x * TILESIZE, 0.06f, z * TILESIZE},
                                  {tile_size, tile_size}, glow);
            }
        }
    }
};

// Render agents as small person-shaped primitives in 3D space
struct RenderAgentsSystem : System<Agent, Transform> {
    static constexpr raylib::Color AGENT_COLOR = {212, 165, 116,
                                                  255};  // warm tan

    void for_each_with(Entity& e, Agent&, Transform& tf, float) override {
        // Hide agents being serviced inside a facility
        if (!e.is_missing<BeingServiced>()) return;

        // Agent position in world space (x = world x, y = world z)
        float wx = tf.position.x;
        float wz = tf.position.y;

        // Simple tall cube as person shape (fast, no mesh generation)
        raylib::DrawCube({wx, 0.2f, wz}, 0.18f, 0.4f, 0.18f, AGENT_COLOR);
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
    void once(float) const override {
        auto* pds = EntityHelper::get_singleton_cmp<PathDrawState>();
        auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!pds || !cam || !grid) return;

        // Use afterhours input API so E2E injected positions are respected
        auto mouse = input::get_mouse_position();
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

struct RenderUISystem : System<> {
    static void draw_text(const std::string& text, float x, float y, float size,
                          raylib::Color color) {
        raylib::DrawTextEx(get_font(), text.c_str(), {x, y}, size, 1.0f, color);
    }

    static void draw_text_bg(const std::string& text, float x, float y,
                             float size, raylib::Color color) {
        auto measure =
            raylib::MeasureTextEx(get_font(), text.c_str(), size, 1.0f);
        raylib::DrawRectangle((int) x - 6, (int) y - 3, (int) measure.x + 12,
                              (int) measure.y + 6, raylib::Color{0, 0, 0, 240});
        draw_text(text, x, y, size, color);
    }

    // Draw text centered horizontally at given y
    static void draw_text_centered(const std::string& text, float y, float size,
                                   raylib::Color color) {
        auto measure =
            raylib::MeasureTextEx(get_font(), text.c_str(), size, 1.0f);
        float x = (DEFAULT_SCREEN_WIDTH - measure.x) / 2.f;
        draw_text(text, x, y, size, color);
    }

    void once(float) const override {
        // Title
        draw_text_bg("Endless Dance Chaos", 10, 10, 26, raylib::WHITE);

        // Clock display
        auto* clock = EntityHelper::get_singleton_cmp<GameClock>();
        if (clock) {
            std::string time_str = clock->format_time();
            std::string phase_str = GameClock::phase_name(clock->get_phase());
            std::string clock_text = fmt::format("{}  {}", time_str, phase_str);
            if (clock->speed == GameSpeed::Paused) {
                clock_text += "  PAUSED";
            }
            draw_text_bg(clock_text, 10, 42, 22,
                         raylib::Color{255, 220, 100, 255});
            auto& vtr = afterhours::testing::VisibleTextRegistry::instance();
            vtr.register_text(time_str);
            vtr.register_text(phase_str);
            if (clock->speed == GameSpeed::Paused) {
                vtr.register_text("PAUSED");
            }
        }

        // Agent count
        int agent_count =
            (int) EntityQuery().whereHasComponent<Agent>().gen_count();
        std::string count_text = fmt::format("Agents: {}", agent_count);
        draw_text_bg(count_text, 10, 128, 22,
                     raylib::Color{200, 220, 255, 255});

        // Death count
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        if (gs && gs->death_count > 0) {
            std::string death_text =
                fmt::format("Deaths: {}/{}", gs->death_count, gs->max_deaths);
            draw_text_bg(death_text, 10, 156, 22,
                         gs->death_count >= gs->max_deaths
                             ? raylib::Color{255, 50, 50, 255}
                             : raylib::Color{255, 180, 80, 255});
        }

        // Overlay indicator
        if (gs && gs->show_data_layer) {
            draw_text_bg("[TAB] Density Overlay ON", 10, 184, 20,
                         raylib::Color{255, 255, 100, 255});
        }

        // FPS (top-right)
        int fps = raylib::GetFPS();
        std::string fps_text = fmt::format("FPS: {}", fps);
        auto fps_measure =
            raylib::MeasureTextEx(get_font(), fps_text.c_str(), 24, 1.0f);
        draw_text_bg(fps_text, DEFAULT_SCREEN_WIDTH - fps_measure.x - 14, 10,
                     24,
                     fps >= 55 ? raylib::Color{100, 255, 100, 255}
                               : raylib::Color{255, 80, 80, 255});

        // Build mode indicator
        auto* pds = EntityHelper::get_singleton_cmp<PathDrawState>();
        if (pds) {
            if (pds->demolish_mode) {
                draw_text_bg("DEMOLISH MODE [X]", 10, 70, 22,
                             raylib::Color{255, 80, 80, 255});
            } else if (pds->is_drawing) {
                draw_text_bg(
                    "Drawing path... (click to confirm, right-click to cancel)",
                    10, 70, 20, raylib::Color{180, 255, 180, 255});
            } else {
                draw_text_bg("Build Path [click] | [X] Demolish", 10, 70, 20,
                             raylib::Color{220, 220, 220, 255});
            }

            // Grid position + tile info readout
            if (pds->hover_valid) {
                auto* grid = EntityHelper::get_singleton_cmp<Grid>();
                if (grid && grid->in_bounds(pds->hover_x, pds->hover_z)) {
                    const auto& tile = grid->at(pds->hover_x, pds->hover_z);
                    std::string hover_text =
                        fmt::format("Grid: ({}, {})  Agents: {}", pds->hover_x,
                                    pds->hover_z, tile.agent_count);
                    draw_text_bg(hover_text, 10, 98, 20,
                                 raylib::Color{200, 200, 200, 255});
                }
            }
        }

        // Game over screen
        if (gs && gs->is_game_over()) {
            // Dark overlay
            raylib::DrawRectangle(0, 0, DEFAULT_SCREEN_WIDTH,
                                  DEFAULT_SCREEN_HEIGHT,
                                  raylib::Color{0, 0, 0, 200});

            // Panel background
            float pw = 460, ph = 260;
            float px = (DEFAULT_SCREEN_WIDTH - pw) / 2.f;
            float py = (DEFAULT_SCREEN_HEIGHT - ph) / 2.f;
            raylib::DrawRectangle((int) px, (int) py, (int) pw, (int) ph,
                                  raylib::Color{20, 20, 30, 240});
            raylib::DrawRectangleLines((int) px, (int) py, (int) pw, (int) ph,
                                       raylib::Color{255, 80, 80, 255});

            // Title
            std::string title = "FESTIVAL SHUT DOWN";
            draw_text_centered(title, py + 24, 32,
                               raylib::Color{255, 80, 80, 255});

            // Stats
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

            float sy = py + 80;
            draw_text_centered(stats1, sy, 20, raylib::WHITE);
            draw_text_centered(stats2, sy + 30, 20, raylib::WHITE);
            draw_text_centered(stats3, sy + 60, 20, raylib::WHITE);
            draw_text_centered(stats4, sy + 90, 20, raylib::WHITE);

            // Restart prompt
            draw_text_centered("Press SPACE to restart", py + ph - 50, 18,
                               raylib::Color{180, 180, 180, 255});

            // Register text for E2E expect_text assertions
            auto& vtr = afterhours::testing::VisibleTextRegistry::instance();
            vtr.register_text(title);
            vtr.register_text("Press SPACE to restart");
        }
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
        raylib::DrawTextEx(get_font(), label.c_str(), {x, y - 18}, 16, 1.0f,
                           raylib::Color{200, 200, 200, 255});

        // Value text
        std::string val_text = fmt::format("{:.2f}", val);
        raylib::DrawTextEx(get_font(), val_text.c_str(), {x + w + 8, y - 2}, 16,
                           1.0f, raylib::Color{255, 255, 100, 255});

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

        // Panel (bottom-left)
        float pw = 300;
        float ph = 180;
        float px = 10;
        float py = DEFAULT_SCREEN_HEIGHT - ph - 10;

        raylib::DrawRectangle((int) px, (int) py, (int) pw, (int) ph,
                              raylib::Color{15, 15, 25, 230});
        raylib::DrawRectangleLines((int) px, (int) py, (int) pw, (int) ph,
                                   raylib::Color{100, 100, 120, 255});

        // Title
        raylib::DrawTextEx(get_font(), "Debug [`]", {px + 10, py + 8}, 18, 1.0f,
                           raylib::Color{255, 200, 80, 255});

        // Speed multiplier slider
        float sx = px + 16;
        float sw = 200;
        gs->speed_multiplier = draw_slider("Agent Speed", sx, py + 55, sw,
                                           gs->speed_multiplier, 0.1f, 20.f);

        // Spawn rate slider (agents/sec, converted to/from interval)
        if (ss) {
            float old_interval = ss->interval;
            float rate = 1.f / ss->interval;
            rate = draw_slider("Spawn Rate (agents/s)", sx, py + 105, sw, rate,
                               0.1f, 20.f);
            ss->interval = 1.f / rate;
            if (ss->interval != old_interval) {
                ss->timer = 0.f;
            }
        }

        // Info
        int agent_count =
            (int) EntityQuery().whereHasComponent<Agent>().gen_count();
        std::string info =
            fmt::format("Agents: {}  Deaths: {}", agent_count, gs->death_count);
        raylib::DrawTextEx(get_font(), info.c_str(), {sx, py + 140}, 14, 1.0f,
                           raylib::Color{160, 160, 160, 255});
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
    sm.register_render_system(std::make_unique<RenderUISystem>());
    sm.register_render_system(std::make_unique<RenderDebugPanelSystem>());
    sm.register_render_system(std::make_unique<EndRenderSystem>());
}
