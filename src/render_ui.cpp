// Render domain: 2D UI (hover, labels, HUD, timeline, minimap, game over).
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "afterhours/src/core/entity_query.h"
#include "afterhours/src/plugins/e2e_testing/visible_text.h"
#include "components.h"
#include "gfx3d.h"
#include "render_helpers.h"
#include "save_system.h"
#include "systems.h"
#include "update_helpers.h"

// Update hover grid position from mouse
struct HoverTrackingSystem : System<> {
    static bool mouse_over_ui(float mx, float my) {
        if (my < 44) return true;
        if (my > DEFAULT_SCREEN_HEIGHT - 54) return true;
        if (mx > DEFAULT_SCREEN_WIDTH - 150) return true;
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

        if (pds->hover_lock_frames > 0) {
            --pds->hover_lock_frames;
            return;
        }

        auto mouse = input::get_mouse_position();

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

// Render facility labels as 2D text projected from 3D positions.
struct RenderFacilityLabelsSystem : System<> {
    static Color fill_bar_color(float ratio) {
        if (ratio < 0.50f) return {80, 200, 80, 220};
        if (ratio < 0.75f) return {220, 200, 40, 220};
        return {220, 60, 40, 220};
    }

    static int facility_agent_count(Grid* grid, float world_x, float world_z) {
        auto [ax, az] = grid->world_to_grid(world_x, world_z);
        int total = 0;
        for (int dz = -1; dz <= 0; dz++)
            for (int dx = -1; dx <= 0; dx++)
                if (grid->in_bounds(ax + dx, az + dz))
                    total += grid->at(ax + dx, az + dz).agent_count;
        return total;
    }

    void once(float) const override {
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
        if (!grid || !cam) return;

        grid->ensure_caches();

        for (const auto& lbl : grid->facility_labels) {
            vec2 screen = get_world_to_screen({lbl.world_x, 0.6f, lbl.world_z},
                                              cam->cam.camera);

            if (screen.x < -50 || screen.x > DEFAULT_SCREEN_WIDTH + 50 ||
                screen.y < -50 || screen.y > DEFAULT_SCREEN_HEIGHT + 50)
                continue;

            float font_size = 13;
            vec2 m =
                measure_text_ex(get_font(), lbl.text, font_size, FONT_SPACING);
            float tx = screen.x - m.x / 2.f;
            float ty = screen.y - m.y / 2.f;

            draw_rect(tx - 3, ty - 2, m.x + 6, m.y + 4, Color{0, 0, 0, 160});
            Color color = {lbl.r, lbl.g, lbl.b, 255};
            draw_text_ex(get_font(), lbl.text, {tx, ty}, font_size,
                         FONT_SPACING, color);

            // Fill bar for serviceable facilities (not stage/gate)
            bool is_facility = (std::strcmp(lbl.text, "WC") == 0 ||
                                std::strcmp(lbl.text, "FOOD") == 0 ||
                                std::strcmp(lbl.text, "MED") == 0);
            if (is_facility) {
                int agents =
                    facility_agent_count(grid, lbl.world_x, lbl.world_z);
                if (agents > 0) {
                    float ratio =
                        static_cast<float>(agents) / FACILITY_MAX_AGENTS;
                    ratio = std::min(ratio, 1.0f);
                    float bar_w = 30.f;
                    float bar_h = 4.f;
                    float bx = screen.x - bar_w / 2.f;
                    float by = ty + m.y + 5.f;
                    draw_rect(bx, by, bar_w, bar_h, Color{0, 0, 0, 140});
                    draw_rect(bx, by, bar_w * ratio, bar_h,
                              fill_bar_color(ratio));
                }
            }
        }
    }
};

struct RenderUISystem : System<> {
    static void draw_text(const std::string& text, float x, float y, float size,
                          Color color) {
        draw_text_ex(get_font(), text.c_str(), {x, y}, size, FONT_SPACING,
                     color);
    }

    static vec2 measure_text(const std::string& text, float size) {
        return measure_text_ex(get_font(), text.c_str(), size, FONT_SPACING);
    }

    static void draw_text_bg(const std::string& text, float x, float y,
                             float size, Color color) {
        auto m = measure_text(text, size);
        draw_rect(x - 6, y - 3, m.x + 12, m.y + 6, Color{0, 0, 0, 240});
        draw_text(text, x, y, size, color);
    }

    static void draw_text_centered(const std::string& text, float y, float size,
                                   Color color) {
        auto m = measure_text(text, size);
        float x = (DEFAULT_SCREEN_WIDTH - m.x) / 2.f;
        draw_text(text, x, y, size, color);
    }

    struct ToolInfo {
        const char* label;
        Color color;
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

        // === TOP BAR ===
        draw_rect(0, 0, DEFAULT_SCREEN_WIDTH, 44, Color{0, 0, 0, 180});
        float bar_x = 12;
        if (clock) {
            std::string time_str = clock->format_time();
            draw_text(time_str, bar_x, 10, 22, Color{255, 255, 255, 255});
            vtr.register_text(time_str);
            bar_x += 90;
            std::string phase_str = GameClock::phase_name(clock->get_phase());
            draw_text(phase_str, bar_x, 11, 20, Color{255, 220, 100, 255});
            vtr.register_text(phase_str);
            bar_x += measure_text(phase_str, 20).x + 16;

            // Speed control icons
            auto mouse = input::get_mouse_position();
            bool mouse_click =
                input::is_mouse_button_pressed(MOUSE_BUTTON_LEFT);
            struct SpeedIcon {
                const char* label;
                GameSpeed speed;
            };
            constexpr SpeedIcon SPEED_ICONS[] = {
                {"||", GameSpeed::Paused},
                {">", GameSpeed::OneX},
                {">>", GameSpeed::TwoX},
                {">>>", GameSpeed::FourX},
            };
            for (int i = 0; i < 4; i++) {
                bool active = (clock->speed == SPEED_ICONS[i].speed);
                Color col = active ? Color{255, 255, 255, 255}
                                   : Color{120, 120, 130, 255};
                auto sm = measure_text(SPEED_ICONS[i].label, 18);
                float icon_w = std::max(sm.x + 8.f, 22.f);
                float ix = bar_x;
                float iy = 8;
                float ih = 28;

                if (mouse_click && !game_is_over() && mouse.x >= ix &&
                    mouse.x <= ix + icon_w && mouse.y >= iy &&
                    mouse.y <= iy + ih) {
                    clock->speed = SPEED_ICONS[i].speed;
                    active = true;
                    col = Color{255, 255, 255, 255};
                }

                if (active) {
                    draw_rect(ix, iy + ih - 2, icon_w, 2,
                              Color{255, 255, 255, 255});
                }
                float tx = ix + (icon_w - sm.x) / 2.f;
                draw_text(SPEED_ICONS[i].label, tx, 12, 18, col);
                bar_x += icon_w + 4;
            }
            bar_x += 8;
        }
        if (gs) {
            std::string death_text =
                fmt::format("Deaths: {}/{}", gs->death_count, gs->max_deaths);
            Color dc = gs->death_count >= 7 ? Color{255, 80, 80, 255}
                                            : Color{255, 255, 255, 255};
            draw_text(death_text, bar_x, 11, 20, dc);
            vtr.register_text(death_text);
            bar_x += 170;
            int agent_count =
                (int) EntityQuery().whereHasComponent<Agent>().gen_count();
            std::string att_text = fmt::format("Attendees: {}", agent_count);
            draw_text(att_text, bar_x, 11, 20, Color{255, 255, 255, 255});
            vtr.register_text(att_text);
            bar_x += measure_text(att_text, 20).x + 20;
        }
        auto* diff = EntityHelper::get_singleton_cmp<DifficultyState>();
        if (diff) {
            std::string day_text = fmt::format("Day {}", diff->day_number);
            draw_text(day_text, bar_x, 11, 20, Color{180, 220, 255, 255});
            vtr.register_text(day_text);
            bar_x += 90;
        }

        {
            auto events = EntityQuery().whereHasComponent<ActiveEvent>().gen();
            for (Entity& ev_e : events) {
                auto& ev = ev_e.get<ActiveEvent>();
                float remain = ev.duration - ev.elapsed;
                std::string ev_text =
                    fmt::format("{} ({:.0f}s)", ev.description, remain);
                draw_text(ev_text, bar_x, 11, 16, Color{255, 200, 80, 255});
                vtr.register_text(ev_text);
                bar_x += measure_text(ev_text, 16).x + 12;
            }
        }

        int fps = get_fps();
        std::string fps_text = fmt::format("FPS: {}", fps);
        auto fps_measure = measure_text(fps_text, 18);
        draw_text(
            fps_text, DEFAULT_SCREEN_WIDTH - 150 - fps_measure.x - 10, 12, 18,
            fps >= 55 ? Color{100, 255, 100, 255} : Color{255, 80, 80, 255});

        // === BUILD BAR ===
        float build_bar_y = DEFAULT_SCREEN_HEIGHT - 54.f;
        draw_rect(0, build_bar_y, DEFAULT_SCREEN_WIDTH, 54,
                  Color{0, 0, 0, 180});
        auto* bs = EntityHelper::get_singleton_cmp<BuilderState>();
        auto mouse = input::get_mouse_position();
        bool mouse_clicked = input::is_mouse_button_pressed(MOUSE_BUTTON_LEFT);
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

                if (mouse_clicked && mouse.x >= ox && mouse.x <= ox + s &&
                    mouse.y >= oy && mouse.y <= oy + s) {
                    bs->tool = static_cast<BuildTool>(i);
                    selected = true;
                }

                Color bg = TOOL_INFO[i].color;
                if (selected) {
                    bg.r = (uint8_t) std::min(255, bg.r + 40);
                    bg.g = (uint8_t) std::min(255, bg.g + 40);
                    bg.b = (uint8_t) std::min(255, bg.b + 40);
                }
                draw_rect(ox, oy, s, s, bg);
                if (selected) {
                    draw_rect_lines(ox, oy, s, s, Color{255, 255, 255, 255});
                }
                auto label_m = measure_text(TOOL_INFO[i].label, 16);
                float lx = ox + (s - label_m.x) / 2.f;
                float ly = oy + (s - label_m.y) / 2.f;
                draw_text(TOOL_INFO[i].label, lx, ly, 16,
                          Color{255, 255, 255, 255});

                if (mouse.x >= ox && mouse.x <= ox + s && mouse.y >= oy &&
                    mouse.y <= oy + s) {
                    auto tip_m = measure_text(TOOL_INFO[i].full_name, 16);
                    float tip_x = ox + (s - tip_m.x) / 2.f;
                    float tip_y = oy - tip_m.y - 8.f;
                    draw_rect(tip_x - 4, tip_y - 2, tip_m.x + 8, tip_m.y + 4,
                              Color{20, 20, 30, 220});
                    draw_text(TOOL_INFO[i].full_name, tip_x, tip_y, 16,
                              Color{255, 255, 255, 255});
                }
            }
        }

        // Toast messages
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

                draw_rect(tx - 8, toast_y - 4, tm.x + 16, tm.y + 8,
                          Color{30, 120, 60, a});
                draw_text_ex(get_font(), toast.text.c_str(), {tx, toast_y}, 20,
                             FONT_SPACING, Color{255, 255, 255, a});
                vtr.register_text(toast.text);
                toast_y += tm.y + 16;
            }
        }

        // NUX banner (persistent hint with dismiss X)
        {
            auto nuxes = EntityQuery().whereHasComponent<NuxHint>().gen();
            for (Entity& ne : nuxes) {
                auto& nux = ne.get<NuxHint>();
                if (!nux.is_active) continue;

                auto tm = measure_text(nux.text, 20);
                float x_btn_size = 18.f;
                float total_w = tm.x + 24 + x_btn_size + 8;
                float bx = (DEFAULT_SCREEN_WIDTH - 150 - total_w) / 2.f;
                float by = 50.f;
                float bh = tm.y + 12;

                // Background with blue accent
                draw_rect(bx, by, total_w, bh, Color{20, 40, 80, 230});
                draw_rect(bx, by, 3, bh, Color{80, 160, 255, 255});

                // Text
                draw_text(nux.text, bx + 12, by + 6, 20,
                          Color{255, 255, 255, 255});
                vtr.register_text(nux.text);

                // X dismiss button
                float xbx = bx + total_w - x_btn_size - 6;
                float xby = by + (bh - x_btn_size) / 2.f;

                auto m = input::get_mouse_position();
                bool hovering = m.x >= xbx && m.x <= xbx + x_btn_size &&
                                m.y >= xby && m.y <= xby + x_btn_size;
                Color xbg = hovering ? Color{200, 60, 60, 255}
                                     : Color{100, 100, 120, 200};
                draw_rect(xbx, xby, x_btn_size, x_btn_size, xbg);

                float cx = xbx + x_btn_size / 2.f;
                float cy = xby + x_btn_size / 2.f;
                float cs = 5.f;
                draw_line(cx - cs, cy - cs, cx + cs, cy + cs,
                          Color{255, 255, 255, 255});
                draw_line(cx - cs, cy + cs, cx + cs, cy - cs,
                          Color{255, 255, 255, 255});

                if (hovering &&
                    input::is_mouse_button_pressed(MOUSE_BUTTON_LEFT)) {
                    nux.was_dismissed = true;
                }

                break;
            }
        }

        // Compass indicator
        {
            auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
            if (cam) {
                float cx = DEFAULT_SCREEN_WIDTH - 150 - 30;
                float cy = 55;
                draw_circle(cx, cy, 16, Color{0, 0, 0, 120});
                float cam_dx =
                    cam->cam.camera.position.x - cam->cam.camera.target.x;
                float cam_dz =
                    cam->cam.camera.position.z - cam->cam.camera.target.z;
                float angle = std::atan2(cam_dz, cam_dx);
                float nx = cx + std::cos(angle) * 12;
                float ny = cy + std::sin(angle) * 12;
                draw_text("N", nx - 6, ny - 8, 14, Color{255, 100, 100, 255});
            }
        }

        // Grid hover info with need breakdown
        auto* pds = EntityHelper::get_singleton_cmp<PathDrawState>();
        if (pds && pds->hover_valid) {
            auto* grid = EntityHelper::get_singleton_cmp<Grid>();
            if (grid && grid->in_bounds(pds->hover_x, pds->hover_z)) {
                const auto& tile = grid->at(pds->hover_x, pds->hover_z);

                std::string hover_text =
                    fmt::format("({}, {})  Agents: {}", pds->hover_x,
                                pds->hover_z, tile.agent_count);

                if (tile.agent_count > 0) {
                    static int cached_hx = -1, cached_hz = -1;
                    static int need_counts[5] = {};
                    if (cached_hx != pds->hover_x ||
                        cached_hz != pds->hover_z) {
                        cached_hx = pds->hover_x;
                        cached_hz = pds->hover_z;
                        std::memset(need_counts, 0, sizeof(need_counts));
                        auto agents = EntityQuery()
                                          .whereHasComponent<Agent>()
                                          .whereHasComponent<Transform>()
                                          .gen();
                        for (Entity& ae : agents) {
                            if (!ae.is_missing<BeingServiced>()) continue;
                            auto& atf = ae.get<Transform>();
                            auto [agx, agz] = grid->world_to_grid(
                                atf.position.x, atf.position.y);
                            if (agx == cached_hx && agz == cached_hz) {
                                int idx =
                                    static_cast<int>(ae.get<Agent>().want);
                                if (idx >= 0 && idx < 5) need_counts[idx]++;
                            }
                        }
                    }

                    const char* names[] = {"bathroom", "food", "watching",
                                           "exiting", "medical"};
                    std::string breakdown;
                    for (int i = 0; i < 5; i++) {
                        if (need_counts[i] > 0) {
                            if (!breakdown.empty()) breakdown += ", ";
                            breakdown +=
                                fmt::format("{} {}", need_counts[i], names[i]);
                        }
                    }
                    if (!breakdown.empty())
                        hover_text += " \xe2\x80\x94 " + breakdown;
                }

                draw_text_bg(hover_text, 10, build_bar_y - 30, 18,
                             Color{200, 200, 200, 255});
            }
        }
        if (gs && gs->show_data_layer) {
            draw_text_bg("[TAB] Density Overlay", 10, build_bar_y - 56, 18,
                         Color{255, 255, 100, 255});
        }
    }
};

// Minimap RenderTexture management
static afterhours::graphics::RenderTextureType g_minimap_texture = {};
static bool g_minimap_initialized = false;
static constexpr int MINIMAP_SIZE = 150;
static constexpr float MINIMAP_SCALE = 150.0f / 52.0f;

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

        draw_rect(sidebar_x, sidebar_y, sidebar_w, sidebar_h,
                  Color{15, 15, 25, 200});

        draw_text_ex(get_font(), "LINEUP", {sidebar_x + 10, 8}, 18,
                     FONT_SPACING, Color{255, 220, 100, 255});
        auto& vtr = afterhours::testing::VisibleTextRegistry::instance();
        vtr.register_text("LINEUP");

        float content_top = 30.f;
        float content_bot = DEFAULT_SCREEN_HEIGHT - 150.f;
        begin_scissor_mode(sidebar_x, content_top, sidebar_w,
                           content_bot - content_top);

        float now_y = sidebar_y + sidebar_h * 0.2f;
        draw_line(sidebar_x, now_y, sidebar_x + sidebar_w, now_y,
                  Color{255, 100, 100, 255});
        draw_text_ex(get_font(), "NOW", {sidebar_x + 6, now_y - 18}, 16,
                     FONT_SPACING, Color{255, 100, 100, 255});

        float now_minutes = clock->game_time_minutes;
        float px_per_minute = 2.4f;

        for (auto& a : sched->schedule) {
            float minutes_from_now = a.start_time_minutes - now_minutes;
            float block_y = now_y + minutes_from_now * px_per_minute;
            float block_h = std::max(a.duration_minutes * px_per_minute, 42.f);

            if (block_y + block_h < content_top || block_y > content_bot)
                continue;

            Color bg =
                a.performing ? Color{255, 217, 61, 80} : Color{40, 40, 60, 180};
            draw_rect(sidebar_x + 4, block_y, sidebar_w - 8, block_h, bg);
            draw_rect_lines(sidebar_x + 4, block_y, sidebar_w - 8, block_h,
                            Color{100, 100, 120, 200});

            std::string label =
                a.performing ? fmt::format("> {}", a.name) : a.name;
            Color name_col = a.performing ? Color{255, 230, 80, 255}
                                          : Color{255, 255, 255, 255};
            draw_text_ex(get_font(), label.c_str(),
                         {sidebar_x + 11, block_y + 5}, 16, FONT_SPACING,
                         Color{0, 0, 0, 120});
            draw_text_ex(get_font(), label.c_str(),
                         {sidebar_x + 10, block_y + 4}, 16, FONT_SPACING,
                         name_col);

            int h = (int) (a.start_time_minutes / 60) % 24;
            int m = (int) a.start_time_minutes % 60;
            std::string info =
                fmt::format("{:02d}:{:02d}  ~{} ppl", h, m, a.expected_crowd);
            draw_text_ex(get_font(), info.c_str(),
                         {sidebar_x + 10, block_y + 24}, 14, FONT_SPACING,
                         Color{190, 190, 210, 255});
        }

        end_scissor_mode();
    }
};

// Minimap rendering at bottom of sidebar.
struct RenderMinimapSystem : System<> {
    static constexpr Color AGENT_DOT_COLORS[] = {
        {126, 207, 192, 255},  // Bathroom
        {244, 164, 164, 255},  // Food
        {255, 217, 61, 255},   // Stage
        {68, 136, 170, 255},   // Exit
        {255, 100, 100, 255},  // MedTent
    };

    void once(float) const override {
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;

        if (!g_minimap_initialized) {
            g_minimap_texture = load_render_texture(MINIMAP_SIZE, MINIMAP_SIZE);
            g_minimap_initialized = true;
            grid->minimap_dirty = true;
        }

        if (grid->minimap_dirty) {
            grid->minimap_dirty = false;

            begin_texture_mode(g_minimap_texture);
            clear_background({152, 212, 168, 255});

            for (int z = 0; z < MAP_SIZE; z++) {
                for (int x = 0; x < MAP_SIZE; x++) {
                    const auto& tile = grid->at(x, z);
                    if (tile.type == TileType::Grass) continue;
                    Color c = tile_day_color(tile.type);
                    float px = x * MINIMAP_SCALE;
                    float py = z * MINIMAP_SCALE;
                    float ps = MINIMAP_SCALE + 0.5f;
                    draw_rect(px, py, ps, ps, c);
                }
            }

            end_texture_mode();
        }

        begin_texture_mode(g_render_texture);

        float sidebar_x = DEFAULT_SCREEN_WIDTH - 150.f;
        float minimap_y = DEFAULT_SCREEN_HEIGHT - MINIMAP_SIZE;

        // Draw cached tile layer
        draw_render_texture(g_minimap_texture, sidebar_x, minimap_y,
                            Color{255, 255, 255, 255});

        // Draw agent dots (per-frame)
        auto agents = EntityQuery()
                          .whereHasComponent<Agent>()
                          .whereHasComponent<Transform>()
                          .gen();
        for (Entity& ae : agents) {
            if (!ae.is_missing<BeingServiced>()) continue;
            auto& tf = ae.get<Transform>();
            float gx = tf.position.x / TILESIZE;
            float gz = tf.position.y / TILESIZE;
            float px = sidebar_x + gx * MINIMAP_SCALE;
            float py = minimap_y + gz * MINIMAP_SCALE;

            int di = static_cast<int>(ae.get<Agent>().want);
            Color dot_col = AGENT_DOT_COLORS[di % 5];
            draw_rect(px, py, 2, 2, dot_col);
        }

        // Draw camera viewport rectangle
        auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
        if (cam) {
            float zoom = cam->cam.camera.position.y;
            float view_tiles = zoom * 1.5f;
            float cam_gx = cam->cam.camera.target.x / TILESIZE;
            float cam_gz = cam->cam.camera.target.z / TILESIZE;
            float mm_cx = sidebar_x + cam_gx * MINIMAP_SCALE;
            float mm_cy = minimap_y + cam_gz * MINIMAP_SCALE;
            float mm_w = view_tiles * MINIMAP_SCALE;
            float mm_h = view_tiles * MINIMAP_SCALE * 0.6f;
            draw_rect_lines(mm_cx - mm_w / 2, mm_cy - mm_h / 2, mm_w, mm_h,
                            Color{255, 255, 255, 255});
        }

        draw_rect_lines(sidebar_x, minimap_y, MINIMAP_SIZE, MINIMAP_SIZE,
                        Color{100, 100, 120, 255});
    }
};

// Game over overlay
struct RenderGameOverSystem : System<> {
    void once(float) const override {
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        if (!gs || !gs->is_game_over()) return;

        auto& vtr = afterhours::testing::VisibleTextRegistry::instance();

        draw_rect(0, 0, DEFAULT_SCREEN_WIDTH, DEFAULT_SCREEN_HEIGHT,
                  Color{0, 0, 0, 200});

        float pw = 460, ph = 360;
        float px = (DEFAULT_SCREEN_WIDTH - pw) / 2.f;
        float py = (DEFAULT_SCREEN_HEIGHT - ph) / 2.f;
        draw_rect(px, py, pw, ph, Color{20, 20, 30, 240});
        draw_rect_lines(px, py, pw, ph, Color{255, 80, 80, 255});

        std::string title = "FESTIVAL SHUT DOWN";
        RenderUISystem::draw_text_centered(title, py + 20, 34,
                                           Color{255, 80, 80, 255});

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
        RenderUISystem::draw_text_centered(stats1, sy, 20,
                                           Color{255, 255, 255, 255});
        RenderUISystem::draw_text_centered(stats2, sy + 28, 20,
                                           Color{255, 255, 255, 255});
        RenderUISystem::draw_text_centered(stats3, sy + 56, 20,
                                           Color{255, 255, 255, 255});
        RenderUISystem::draw_text_centered(stats4, sy + 84, 20,
                                           Color{255, 255, 255, 255});

        save::MetaProgress meta;
        save::load_meta(meta);
        float my = sy + 124;
        draw_line(px + 20, my, px + pw - 20, my, Color{100, 100, 120, 200});
        my += 8;
        RenderUISystem::draw_text_centered("--- All-Time Records ---", my, 16,
                                           Color{180, 200, 255, 255});
        my += 24;
        RenderUISystem::draw_text_centered(
            fmt::format("Best Day: {}  |  Best Served: {}", meta.best_day,
                        meta.best_agents_served),
            my, 16, Color{160, 180, 220, 255});
        my += 22;
        RenderUISystem::draw_text_centered(
            fmt::format("Peak Attendees: {}  |  Runs: {}",
                        meta.best_max_attendees, meta.total_runs),
            my, 16, Color{160, 180, 220, 255});

        RenderUISystem::draw_text_centered("Press SPACE to restart",
                                           py + ph - 40, 20,
                                           Color{180, 180, 180, 255});

        vtr.register_text(title);
        vtr.register_text("Press SPACE to restart");
    }
};

struct EndRenderSystem : System<> {
    void once(float) const override {
        end_texture_mode();

        begin_drawing();
        clear_background(Color{0, 0, 0, 255});
        draw_render_texture(g_render_texture, 0, 0, Color{255, 255, 255, 255});
        end_drawing();
    }
};

void register_render_ui_systems(SystemManager& sm) {
    sm.register_render_system(std::make_unique<HoverTrackingSystem>());
    sm.register_render_system(std::make_unique<RenderFacilityLabelsSystem>());
    sm.register_render_system(std::make_unique<RenderUISystem>());
    sm.register_render_system(std::make_unique<RenderTimelineSidebarSystem>());
    sm.register_render_system(std::make_unique<RenderMinimapSystem>());
    sm.register_render_system(std::make_unique<RenderGameOverSystem>());
}

void register_render_end_system(SystemManager& sm) {
    sm.register_render_system(std::make_unique<EndRenderSystem>());
}
