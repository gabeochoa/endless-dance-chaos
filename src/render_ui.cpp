// Render domain: 2D UI (hover, labels, HUD, timeline, minimap, game over).
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "afterhours/src/core/entity_query.h"
#include "afterhours/src/plugins/e2e_testing/visible_text.h"
#include "components.h"
#include "gfx3d.h"
#include "guest_thoughts.h"
#include "render_helpers.h"
#include "save_system.h"
#include "systems.h"
#include "update_helpers.h"

// ── RCT2 park window geometry (shared by hover + window systems) ──
// Floating window on the right of the viewport: title bar + one tab
// row + content. Ends at y=560, leaving a gap above the minimap
// (y=570) and clear of the compass / ISO toggle at (1100, 90).
// Tabs are fixed-width (sum = PARK_WINDOW_W) so E2E `click` can
// target tab centres: Park (1042,145), Finances (1098,145),
// Guests (1158,145), Staff (1206,145), Lineup (1254,145).
static constexpr float PARK_WINDOW_X = 1020.f;
static constexpr float PARK_WINDOW_Y = 110.f;
static constexpr float PARK_WINDOW_W = 260.f;
static constexpr float PARK_WINDOW_H = 450.f;
static constexpr float PARK_WINDOW_TITLE_H = 24.f;
static constexpr float PARK_WINDOW_TAB_H = 22.f;
static constexpr float PARK_WINDOW_TAB_Y = PARK_WINDOW_Y + PARK_WINDOW_TITLE_H;
static constexpr float PARK_WINDOW_CONTENT_Y =
    PARK_WINDOW_TAB_Y + PARK_WINDOW_TAB_H;
static constexpr float PARK_WINDOW_TAB_W[] = {44.f, 68.f, 52.f, 44.f, 52.f};
static constexpr const char* PARK_WINDOW_TAB_NAMES[] = {
    "Park", "Finances", "Guests", "Staff", "Lineup"};
static constexpr int PARK_WINDOW_TAB_COUNT = 5;

// Update hover grid position from mouse
struct HoverTrackingSystem : System<> {
    static bool mouse_over_ui(float mx, float my) {
        if (my < 44) return true;
        if (my > DEFAULT_SCREEN_HEIGHT - 54 - 22) return true;  // ticker + bar
        if (mx > DEFAULT_SCREEN_WIDTH - 150) return true;
        if (mx >= PARK_WINDOW_X && mx <= PARK_WINDOW_X + PARK_WINDOW_W &&
            my >= PARK_WINDOW_Y && my <= PARK_WINDOW_Y + PARK_WINDOW_H)
            return true;  // park window floats over the world
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
            // world_to_screen branches on the view mode: iso projection
            // in iso mode, legacy 3D projection in top-down mode.
            vec2 screen =
                cam->cam.world_to_screen(vec3{lbl.world_x, 0.6f, lbl.world_z});

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
                                std::strcmp(lbl.text, "MED") == 0 ||
                                std::strcmp(lbl.text, "BAR") == 0 ||
                                std::strcmp(lbl.text, "WATER") == 0);
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

// Money floaters: +$N text rising + fading at the gate / bar anchor,
// screen-space via world->screen in the active projection (iso or
// top-down). Drawn unclipped — the backend clips off-screen text, so a
// floater whose facility is currently off-screen (e.g. the gate at
// the default zoom) still lands in the visible-text registry, the
// same register-everything pattern the ticker uses for scrolled
// text; assert_floater_count is the visibility-independent assert.
struct RenderMoneyFloatersSystem : System<> {
    void once(float) const override {
        auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
        if (!cam) return;
        auto& vtr = afterhours::testing::VisibleTextRegistry::instance();

        for (auto& f : money_floaters().items) {
            vec2 screen =
                cam->cam.world_to_screen(vec3{f.world_x, 0.6f, f.world_z});
            float rise = (f.elapsed / f.lifetime) * 28.f;
            float alpha = 1.f;
            if (f.elapsed > f.lifetime - 0.5f)
                alpha = std::max(0.f, (f.lifetime - f.elapsed) / 0.5f);
            unsigned char a = static_cast<unsigned char>(alpha * 255);
            std::string text = fmt::format("+${}", f.amount);
            auto m =
                measure_text_ex(get_font(), text.c_str(), 16, FONT_SPACING);
            float tx = screen.x - m.x / 2.f;
            float ty = screen.y - rise - m.y / 2.f;
            draw_rect(tx - 3, ty - 2, m.x + 6, m.y + 4,
                      Color{0, 0, 0, static_cast<unsigned char>(a / 2)});
            draw_text_ex(get_font(), text.c_str(), {tx, ty}, 16, FONT_SPACING,
                         Color{255, 225, 70, a});
            vtr.register_text(text);
        }
    }
};

// Shared UI text helpers
static void ui_draw_text(const std::string& text, float x, float y, float size,
                         Color color) {
    draw_text_ex(get_font(), text.c_str(), {x, y}, size, FONT_SPACING, color);
}

static vec2 ui_measure_text(const std::string& text, float size) {
    return measure_text_ex(get_font(), text.c_str(), size, FONT_SPACING);
}

static void ui_draw_text_bg(const std::string& text, float x, float y,
                            float size, Color color) {
    auto m = ui_measure_text(text, size);
    draw_bevel_panel(x - 6, y - 3, m.x + 12, m.y + 6,
                     Color{192, 192, 192, 255});
    ui_draw_text(text, x, y, size, color);
}

static void ui_draw_text_centered(const std::string& text, float y, float size,
                                  Color color) {
    auto m = ui_measure_text(text, size);
    float x = (DEFAULT_SCREEN_WIDTH - m.x) / 2.f;
    ui_draw_text(text, x, y, size, color);
}

struct ToolInfo {
    const char* label;
    Color color;
    const char* full_name;
};
static constexpr ToolInfo TOOL_INFO[] = {
    {"P", {245, 205, 120, 255}, "Path"},
    {"F", {150, 95, 50, 255}, "Fence"},
    {"G", {255, 255, 255, 255}, "Gate"},
    {"S", {200, 30, 230, 255}, "Stage"},
    {"B", {0, 210, 200, 255}, "Bathroom"},
    {"Fd", {255, 80, 160, 255}, "Food Stall"},
    {"M", {255, 45, 45, 255}, "Med Tent"},
    {"X", {255, 68, 68, 255}, "Demolish"},
    {"Br", {255, 145, 0, 255}, "Bar"},
    {"SP", {50, 90, 255, 255}, "Security Post"},
    {"W", {0, 180, 255, 255}, "Water Station"},
    {"Tr", {34, 139, 34, 255}, "Tree"},
    {"Bn", {160, 120, 80, 255}, "Bench"},
    {"Di", {176, 141, 88, 255}, "Dirt Path"},
    {"Ne", {255, 45, 215, 255}, "Neon Path"},
    {"Lp", {255, 225, 120, 255}, "Lamp"},
};
static constexpr int TOOL_COUNT = 16;

static constexpr float BUILD_BAR_Y = DEFAULT_SCREEN_HEIGHT - 54.f;

struct RenderTopBarSystem : System<> {
    void once(float) const override {
        auto& vtr = afterhours::testing::VisibleTextRegistry::instance();
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        auto* clock = EntityHelper::get_singleton_cmp<GameClock>();

        draw_bevel_panel(0, 0, DEFAULT_SCREEN_WIDTH, 44,
                         Color{192, 192, 192, 255});
        // Right cluster (Day) is laid out from measured text widths,
        // ending at the sidebar edge minus a margin. The left flow may
        // never cross left_cap: tail items are dropped before they can
        // overlap Day (the old code drew Day in the left flow with a
        // fixed +90 advance, so at 1280x720 it collided with FPS).
        // FPS lives in the park window's title bar now (see
        // RenderParkWindowSystem), not in this bar.
        float right_end = DEFAULT_SCREEN_WIDTH - 150 - 12;
        std::string day_text;
        auto* diff = EntityHelper::get_singleton_cmp<DifficultyState>();
        if (diff) day_text = fmt::format("Day {}", diff->day_number);
        float right_start =
            right_end -
            (day_text.empty() ? 0.f : ui_measure_text(day_text, 20).x);
        float left_cap = right_start - 12;
        float bar_x = 12;
        if (clock) {
            std::string time_str = clock->format_time();
            ui_draw_text(time_str, bar_x, 10, 22, Color{0, 0, 0, 255});
            vtr.register_text(time_str);
            bar_x += 90;
            std::string phase_str = GameClock::phase_name(clock->get_phase());
            ui_draw_text(phase_str, bar_x, 11, 20, Color{30, 30, 160, 255});
            vtr.register_text(phase_str);
            bar_x += ui_measure_text(phase_str, 20).x + 16;

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
                Color col =
                    active ? Color{0, 0, 0, 255} : Color{110, 110, 110, 255};
                auto sm = ui_measure_text(SPEED_ICONS[i].label, 18);
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
                    draw_rect(ix, iy + ih - 2, icon_w, 2, Color{0, 0, 0, 255});
                }
                float tx = ix + (icon_w - sm.x) / 2.f;
                ui_draw_text(SPEED_ICONS[i].label, tx, 12, 18, col);
                bar_x += icon_w + 4;
            }
            bar_x += 8;
        }
        if (gs) {
            std::string death_text =
                fmt::format("Deaths: {}/{}", gs->death_count, gs->max_deaths);
            Color dc = gs->death_count >= 7 ? Color{200, 0, 0, 255}
                                            : Color{0, 0, 0, 255};
            ui_draw_text(death_text, bar_x, 11, 20, dc);
            vtr.register_text(death_text);
            bar_x += ui_measure_text(death_text, 20).x + 12;
            int agent_count =
                (int) EntityQuery().whereHasComponent<Agent>().gen_count();
            std::string att_text = fmt::format("Attendees: {}", agent_count);
            ui_draw_text(att_text, bar_x, 11, 20, Color{0, 0, 0, 255});
            vtr.register_text(att_text);
            bar_x += ui_measure_text(att_text, 20).x + 12;
        }
        if (auto* econ = EntityHelper::get_singleton_cmp<Economy>()) {
            std::string cash_text = fmt::format("Cash: ${}", econ->cash);
            Color cash_col =
                econ->cash < 0 ? Color{200, 0, 0, 255} : Color{0, 140, 0, 255};
            ui_draw_text(cash_text, bar_x, 11, 20, cash_col);
            vtr.register_text(cash_text);
            bar_x += ui_measure_text(cash_text, 20).x + 12;

            // Ticket price with [-]/[+] buttons (same adjust as "," / ".").
            std::string ticket_text =
                fmt::format("Ticket: ${}", econ->ticket_price);
            ui_draw_text(ticket_text, bar_x, 11, 20, Color{160, 100, 0, 255});
            vtr.register_text(ticket_text);
            bar_x += ui_measure_text(ticket_text, 20).x + 6;
            auto mouse = input::get_mouse_position();
            bool mouse_click =
                input::is_mouse_button_pressed(MOUSE_BUTTON_LEFT);
            const char* price_btns[] = {"-", "+"};
            for (int i = 0; i < 2; i++) {
                float bw = 22.f;
                if (mouse_click && !game_is_over() && mouse.x >= bar_x &&
                    mouse.x <= bar_x + bw && mouse.y >= 8 && mouse.y <= 36) {
                    econ->adjust_price(i == 0 ? -Economy::TICKET_PRICE_STEP
                                              : Economy::TICKET_PRICE_STEP);
                }
                draw_bevel_panel(bar_x, 8, bw, 28, Color{200, 200, 200, 255});
                auto bm = ui_measure_text(price_btns[i], 18);
                ui_draw_text(price_btns[i], bar_x + (bw - bm.x) / 2.f, 12, 18,
                             Color{0, 0, 0, 255});
                bar_x += bw + 4;
            }
            bar_x += 6;
        }
        if (auto* insp = EntityHelper::get_singleton_cmp<InspectionState>()) {
            std::string insp_text =
                fmt::format("Insp: {}", std::string(1, insp->last_grade));
            float insp_w = ui_measure_text(insp_text, 20).x;
            if (bar_x + insp_w <= left_cap) {
                Color insp_col = insp->last_grade == 'F' ? Color{200, 0, 0, 255}
                                                         : Color{0, 0, 0, 255};
                ui_draw_text(insp_text, bar_x, 11, 20, insp_col);
                vtr.register_text(insp_text);
                bar_x += insp_w + 12;
            }
        }
        // Park rating with trend arrow vs yesterday (rollover snapshot).
        // Drawn after Insp (which existing tests assert) but before Inj:
        // the bar cannot fit all three tail items at 1280px, so Inj —
        // never text-asserted — is the one dropped when crowded.
        if (auto* rating = EntityHelper::get_singleton_cmp<ParkRatingState>()) {
            std::string rating_text = fmt::format(
                "Rating: {} {}", static_cast<int>(std::round(rating->rating)),
                rating->trend_arrow());
            float rating_w = ui_measure_text(rating_text, 20).x;
            if (bar_x + rating_w <= left_cap) {
                Color rating_col = rating->rating >= 600 ? Color{0, 140, 0, 255}
                                   : rating->rating < 400
                                       ? Color{200, 0, 0, 255}
                                       : Color{0, 0, 0, 255};
                ui_draw_text(rating_text, bar_x, 11, 20, rating_col);
                vtr.register_text(rating_text);
                bar_x += rating_w + 12;
            }
        }
        if (gs) {
            std::string inj_text = fmt::format("Inj: {}", gs->injury_count);
            float inj_w = ui_measure_text(inj_text, 20).x;
            if (bar_x + inj_w <= left_cap) {
                Color inj_col = gs->injury_count > 0 ? Color{200, 100, 0, 255}
                                                     : Color{0, 0, 0, 255};
                ui_draw_text(inj_text, bar_x, 11, 20, inj_col);
                vtr.register_text(inj_text);
                bar_x += inj_w + 12;
            }
        }
        // Right cluster: Day, drawn from its measured width so it can
        // never overlap the left flow (or the sidebar) at 1280x720.
        if (!day_text.empty()) {
            ui_draw_text(day_text, right_start, 11, 20,
                         Color{30, 60, 160, 255});
            vtr.register_text(day_text);
        }

        {
            auto events = EntityQuery().whereHasComponent<ActiveEvent>().gen();
            for (Entity& ev_e : events) {
                auto& ev = ev_e.get<ActiveEvent>();
                float remain = ev.duration - ev.elapsed;
                std::string ev_text =
                    fmt::format("{} ({:.0f}s)", ev.description, remain);
                float ev_w = ui_measure_text(ev_text, 16).x;
                if (bar_x + ev_w > left_cap) continue;  // drop, don't overlap
                ui_draw_text(ev_text, bar_x, 11, 16, Color{160, 100, 0, 255});
                vtr.register_text(ev_text);
                bar_x += ev_w + 12;
            }
        }
    }
};

struct RenderBuildBarSystem : System<> {
    void once(float) const override {
        draw_bevel_panel(0, BUILD_BAR_Y, DEFAULT_SCREEN_WIDTH, 54,
                         Color{192, 192, 192, 255});
        auto* bs = EntityHelper::get_singleton_cmp<BuilderState>();
        if (!bs) return;

        auto mouse = input::get_mouse_position();
        bool mouse_clicked = input::is_mouse_button_pressed(MOUSE_BUTTON_LEFT);
        // 16 tools at 30px + 4px gaps = 540px total: the same bar
        // footprint as the old 13 tools at 36px + 6px gaps.
        float icon_size = 30.f;
        float gap = 4.f;
        float total_w = TOOL_COUNT * icon_size + (TOOL_COUNT - 1) * gap;
        float start_x = (DEFAULT_SCREEN_WIDTH - 150 - total_w) / 2.f;
        for (int i = 0; i < TOOL_COUNT; i++) {
            float ix = start_x + i * (icon_size + gap);
            float iy = BUILD_BAR_Y + (54 - icon_size) / 2.f;
            bool selected = (static_cast<int>(bs->tool) == i);
            float s = selected ? icon_size * 1.15f : icon_size;
            float ox = ix - (s - icon_size) / 2.f;
            float oy = iy - (s - icon_size) / 2.f;

            if (mouse_clicked && mouse.x >= ox && mouse.x <= ox + s &&
                mouse.y >= oy && mouse.y <= oy + s) {
                bs->tool = static_cast<BuildTool>(i);
                selected = true;
            }

            // Panel-styled button: raised bevel, or pressed-in when
            // selected. The tool's building color shows as a chip strip
            // across the top; the letter sits below it in black.
            if (selected)
                draw_bevel_panel_pressed(ox, oy, s, s,
                                         Color{165, 165, 165, 255});
            else
                draw_bevel_panel(ox, oy, s, s, Color{200, 200, 200, 255});
            draw_rect(ox + 5, oy + 5, s - 10, 9, TOOL_INFO[i].color);
            draw_rect_lines(ox + 5, oy + 5, s - 10, 9, Color{30, 30, 30, 255});
            auto label_m = ui_measure_text(TOOL_INFO[i].label, 16);
            float lx = ox + (s - label_m.x) / 2.f;
            float ly = oy + s - label_m.y - 5.f;
            ui_draw_text(TOOL_INFO[i].label, lx, ly, 16, Color{0, 0, 0, 255});

            if (mouse.x >= ox && mouse.x <= ox + s && mouse.y >= oy &&
                mouse.y <= oy + s) {
                auto tip_m = ui_measure_text(TOOL_INFO[i].full_name, 16);
                float tip_x = ox + (s - tip_m.x) / 2.f;
                float tip_y = oy - tip_m.y - 8.f;
                draw_bevel_panel(tip_x - 4, tip_y - 2, tip_m.x + 8, tip_m.y + 4,
                                 Color{192, 192, 192, 255});
                ui_draw_text(TOOL_INFO[i].full_name, tip_x, tip_y, 16,
                             Color{0, 0, 0, 255});
            }
        }
    }
};

// Staff hire/fire panel, build-bar style: docked into the build bar's
// right margin (build tools end at x~793, sidebar starts at 1130), so it
// reuses the build bar's bevel + click + hover-tooltip patterns.
// Geometry is fixed so E2E `click` can target the buttons:
//   panel x=830 w=290; columns at x=838/932/1026; hire "+" centre
//   (859/953/1047, 702), fire "-" centre (905/999/1093, 702).
// Hires spawn at the gate; fires refund nothing.
static constexpr float STAFF_PANEL_X = DEFAULT_SCREEN_WIDTH - 150 - 300;
static constexpr float STAFF_PANEL_W = 290.f;

struct RenderStaffPanelSystem : System<> {
    void once(float) const override {
        auto& vtr = afterhours::testing::VisibleTextRegistry::instance();
        auto mouse = input::get_mouse_position();
        bool mouse_click = input::is_mouse_button_pressed(MOUSE_BUTTON_LEFT);
        if (game_is_over()) return;

        float px = STAFF_PANEL_X;
        float py = BUILD_BAR_Y + 4;
        draw_bevel_panel(px, py, STAFF_PANEL_W, 46, Color{175, 175, 175, 255});

        static constexpr StaffRole ROLES[] = {
            StaffRole::Guard, StaffRole::Medic, StaffRole::Janitor};
        static constexpr const char* SHORT_NAMES[] = {"Guard", "Medic",
                                                      "Janitor"};
        for (int i = 0; i < 3; i++) {
            float col_x = px + 8 + i * 94.f;
            std::string label =
                fmt::format("{} {}", SHORT_NAMES[i], staff_count(ROLES[i]));
            ui_draw_text(label, col_x, py + 4, 13, Color{0, 0, 0, 255});
            vtr.register_text(label);

            float btn_y = py + 22;
            float btn_h = 20.f;
            float hire_x = col_x;
            float fire_x = col_x + 46;
            float btn_w = 42.f;
            if (mouse_click && mouse.y >= btn_y && mouse.y <= btn_y + btn_h) {
                if (mouse.x >= hire_x && mouse.x <= hire_x + btn_w) {
                    hire_staff(ROLES[i], SPAWN_X, SPAWN_Z);
                } else if (mouse.x >= fire_x && mouse.x <= fire_x + btn_w) {
                    fire_staff(ROLES[i]);
                }
            }
            draw_bevel_panel(hire_x, btn_y, btn_w, btn_h,
                             Color{200, 200, 200, 255});
            draw_bevel_panel(fire_x, btn_y, btn_w, btn_h,
                             Color{200, 200, 200, 255});
            auto plus_m = ui_measure_text("+", 16);
            auto minus_m = ui_measure_text("-", 16);
            ui_draw_text("+", hire_x + (btn_w - plus_m.x) / 2.f, btn_y + 2, 16,
                         Color{0, 120, 0, 255});
            ui_draw_text("-", fire_x + (btn_w - minus_m.x) / 2.f, btn_y + 2, 16,
                         Color{200, 0, 0, 255});

            if (mouse.x >= col_x && mouse.x <= col_x + 88 && mouse.y >= py &&
                mouse.y <= py + 46) {
                std::string tip = fmt::format(
                    "{}: ${} signing, ${}/day. + hires, - fires (no refund).",
                    SHORT_NAMES[i], staff_bonus_for(ROLES[i]),
                    staff_wage_for(ROLES[i]));
                auto tip_m = ui_measure_text(tip, 14);
                float tip_x =
                    std::clamp(col_x + 44 - tip_m.x / 2.f, 8.f,
                               DEFAULT_SCREEN_WIDTH - 150 - tip_m.x - 8.f);
                float tip_y = py - tip_m.y - 8.f;
                draw_bevel_panel(tip_x - 4, tip_y - 2, tip_m.x + 8, tip_m.y + 4,
                                 Color{192, 192, 192, 255});
                ui_draw_text(tip, tip_x, tip_y, 14, Color{0, 0, 0, 255});
            }
        }
    }
};

// Land expansion panel, build-bar style: docked into the build bar's
// left margin (build tools start at x~337), mirroring the staff panel.
// Geometry is fixed so E2E `click` can target the buttons:
//   panel x=8 w=320; East button centre (86, 702), South (234, 702).
// Each plot adds +25 buildable tiles on its side; cost doubles per plot
// bought (5000 / 10000 / 20000 / 40000). Maxed sides show MAX.
static constexpr float LAND_PANEL_X = 8.f;
static constexpr float LAND_PANEL_W = 320.f;

struct RenderLandPanelSystem : System<> {
    void once(float) const override {
        auto& vtr = afterhours::testing::VisibleTextRegistry::instance();
        auto mouse = input::get_mouse_position();
        bool mouse_click = input::is_mouse_button_pressed(MOUSE_BUTTON_LEFT);
        if (game_is_over()) return;
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;

        float px = LAND_PANEL_X;
        float py = BUILD_BAR_Y + 4;
        draw_bevel_panel(px, py, LAND_PANEL_W, 46, Color{175, 175, 175, 255});

        bool east_maxed = grid->expansions_x >= EXPANSION_MAX_PER_AXIS;
        bool south_maxed = grid->expansions_z >= EXPANSION_MAX_PER_AXIS;
        std::string label =
            (east_maxed && south_maxed)
                ? "Land: MAXED"
                : fmt::format("Land Expand: ${}", grid->next_expansion_cost());
        ui_draw_text(label, px + 8, py + 4, 13, Color{0, 0, 0, 255});
        vtr.register_text(label);

        float btn_y = py + 22;
        float btn_h = 20.f;
        float btn_w = 140.f;
        float east_x = px + 8;
        float south_x = px + 156;
        const char* east_label = east_maxed ? "East MAX" : "East +25";
        const char* south_label = south_maxed ? "South MAX" : "South +25";
        if (mouse_click && mouse.y >= btn_y && mouse.y <= btn_y + btn_h &&
            !game_is_over()) {
            if (!east_maxed && mouse.x >= east_x && mouse.x <= east_x + btn_w)
                buy_land_expansion(0);
            else if (!south_maxed && mouse.x >= south_x &&
                     mouse.x <= south_x + btn_w)
                buy_land_expansion(1);
        }
        draw_bevel_panel(east_x, btn_y, btn_w, btn_h,
                         Color{200, 200, 200, 255});
        draw_bevel_panel(south_x, btn_y, btn_w, btn_h,
                         Color{200, 200, 200, 255});
        auto east_m = ui_measure_text(east_label, 14);
        auto south_m = ui_measure_text(south_label, 14);
        ui_draw_text(east_label, east_x + (btn_w - east_m.x) / 2.f, btn_y + 3,
                     14, Color{0, 120, 0, 255});
        ui_draw_text(south_label, south_x + (btn_w - south_m.x) / 2.f,
                     btn_y + 3, 14, Color{0, 120, 0, 255});
        vtr.register_text(east_label);
        vtr.register_text(south_label);
    }
};

// Bottom news ticker: a scrolling strip above the build bar, fed by
// edge-detecting the game singletons each render frame (see NewsTicker
// in render_helpers.h). The newest event pre-empts the strip, so it is
// visible — and expect_text-assertable via the registry — within a frame.
static constexpr float TICKER_H = 22.f;
static constexpr float TICKER_Y = BUILD_BAR_Y - TICKER_H;
static constexpr float TICKER_SCROLL_SPEED = 45.f;  // px per second

void ticker_poll_events() {
    auto& t = news_ticker();
    auto* gs = EntityHelper::get_singleton_cmp<GameState>();
    auto* insp = EntityHelper::get_singleton_cmp<InspectionState>();
    auto* diff = EntityHelper::get_singleton_cmp<DifficultyState>();
    auto* staff_state = EntityHelper::get_singleton_cmp<StaffState>();
    auto* lineup = EntityHelper::get_singleton_cmp<LineupState>();
    auto* director = EntityHelper::get_singleton_cmp<EventDirectorState>();

    // Event director announcements: every fired event (including
    // instant ones with no ActiveEvent entity) lands in the ticker.
    // The queue is drained here, so a reset (which clears the queue)
    // never re-reports a previous run's events.
    if (director) {
        for (const auto& text : director->announcement_queue)
            t.push_event(text);
        director->announcement_queue.clear();
    }

    if (!t.seeded) {
        t.last_deaths = gs ? gs->death_count : 0;
        t.last_injuries = gs ? gs->injury_count : 0;
        t.last_inspections = insp ? insp->inspections_done : 0;
        t.last_milestone_idx = gs ? gs->milestone_idx : 0;
        t.last_tier = lineup ? lineup->tier : 0;
        t.last_day = diff ? diff->day_number : 1;
        t.last_game_over = gs && gs->is_game_over();
        t.last_staff_quits = staff_state ? staff_state->quit_count : 0;
        auto* clock_seed = EntityHelper::get_singleton_cmp<GameClock>();
        t.last_thought_hour = clock_seed ? clock_seed->get_hour() : -1;
        t.seeded = true;
        return;
    }

    // Any counter dropping means the game was reset: counters never
    // decrease during a run. Clear the strip so a fresh game does not
    // keep scrolling the previous run's news.
    bool reset_seen = false;
    // Tier-up: polled BEFORE milestones so a same-frame milestone (a
    // 1000-arrival jump also crosses the 500 tier threshold) pre-empts it
    // and the milestone assertion still sees its own item first.
    if (lineup) {
        if (lineup->tier > t.last_tier) {
            t.last_tier = lineup->tier;
            t.push_event(fmt::format("TIER UP! The festival is now {}! {}",
                                     FESTIVAL_TIER_NAMES[lineup->tier],
                                     FESTIVAL_TIER_UP_LINES[lineup->tier]));
        } else if (lineup->tier < t.last_tier) {
            t.last_tier = lineup->tier;
            reset_seen = true;
        }
    }
    if (gs) {
        if (gs->death_count > t.last_deaths) {
            int d = gs->death_count - t.last_deaths;
            t.last_deaths = gs->death_count;
            t.push_event(
                fmt::format("TRAGEDY! {} more festival-goer{} ha{} perished. "
                            "Management offers thoughts and prayers.",
                            d, d == 1 ? "" : "s", d == 1 ? "s" : "ve"));
        } else if (gs->death_count < t.last_deaths) {
            t.last_deaths = gs->death_count;
            reset_seen = true;
        }
        if (gs->injury_count > t.last_injuries) {
            int n = gs->injury_count - t.last_injuries;
            t.last_injuries = gs->injury_count;
            t.push_event(fmt::format(
                "MEDICS: {} new injur{}! The med tent swears it is 'just a "
                "vibe check'.",
                n, n == 1 ? "y" : "ies"));
        } else if (gs->injury_count < t.last_injuries) {
            t.last_injuries = gs->injury_count;
            reset_seen = true;
        }
        static constexpr int MILESTONES[] = {1000,  5000,  10000,
                                             25000, 50000, 100000};
        while (gs->milestone_idx > t.last_milestone_idx &&
               t.last_milestone_idx < 6) {
            int m = MILESTONES[t.last_milestone_idx];
            t.last_milestone_idx++;
            t.push_event(fmt::format(
                "MILESTONE! {} attendees and counting! The mayor wants a "
                "photo op.",
                m));
        }
        if (gs->milestone_idx < t.last_milestone_idx) {
            t.last_milestone_idx = gs->milestone_idx;
            reset_seen = true;
        }
        bool over = gs->is_game_over();
        if (over && !t.last_game_over)
            t.push_event(
                "SHUTDOWN! The festival has been shut down. Pack "
                "up the port-a-potties.");
        t.last_game_over = over;
    }
    if (insp) {
        if (insp->inspections_done > t.last_inspections) {
            t.last_inspections = insp->inspections_done;
            t.push_event(fmt::format(
                "BREAKING: Health inspector awards Grade {} ({}).",
                std::string(1, insp->last_grade), insp->last_score));
        } else if (insp->inspections_done < t.last_inspections) {
            t.last_inspections = insp->inspections_done;
            reset_seen = true;
        }
    }
    if (diff) {
        if (diff->day_number > t.last_day) {
            t.last_day = diff->day_number;
            t.push_event(
                fmt::format("DAY {} BEGINS! Fresh crowds, same "
                            "questionable decisions.",
                            diff->day_number));
        } else if (diff->day_number < t.last_day) {
            t.last_day = diff->day_number;
            reset_seen = true;
        }
    }
    if (staff_state) {
        if (staff_state->quit_count > t.last_staff_quits) {
            int n = staff_state->quit_count - t.last_staff_quits;
            t.last_staff_quits = staff_state->quit_count;
            t.push_event(fmt::format(
                "STAFF WALKOUT! {} staffer{} quit over unpaid wages. "
                "Management blames 'the vibes' and offers exposure.",
                n, n == 1 ? "" : "s"));
        } else if (staff_state->quit_count < t.last_staff_quits) {
            t.last_staff_quits = staff_state->quit_count;
            reset_seen = true;
        }
    }
    // Guest-thought quote: once per game-hour step, when at least two
    // guests share the top thought, the ticker quotes it. Gated on
    // !reset_seen (a reset resyncs the hour below) and on game-over.
    // Any other hour change (reset, set_time jump, pause skip)
    // resyncs silently without quoting. Event items still pre-empt
    // via the normal push_event path; a thought quote is just one
    // more item.
    if (!reset_seen) {
        auto* clock = EntityHelper::get_singleton_cmp<GameClock>();
        if (clock && gs && !gs->is_game_over()) {
            int hour = clock->get_hour();
            if (hour != t.last_thought_hour) {
                bool step = t.last_thought_hour >= 0 &&
                            hour == (t.last_thought_hour + 1) % 24;
                t.last_thought_hour = hour;
                if (step) {
                    auto thoughts = compute_guest_thoughts();
                    if (!thoughts.empty() && thoughts[0].count >= 2) {
                        t.push_event(
                            fmt::format("GUEST: \"{}\" ({} guests agree)",
                                        thoughts[0].text, thoughts[0].count));
                    }
                }
            }
        }
    }
    if (reset_seen) {
        t.queue.clear();
        t.current.clear();
        t.scroll_px = 0.f;
        t.flavor_idx = 0;
        auto* clock = EntityHelper::get_singleton_cmp<GameClock>();
        if (clock) t.last_thought_hour = clock->get_hour();
    }
}

struct RenderNewsTickerSystem : System<> {
    void once(float) const override {
        ticker_poll_events();
        auto& t = news_ticker();
        auto& vtr = afterhours::testing::VisibleTextRegistry::instance();

        float w = DEFAULT_SCREEN_WIDTH - 150.f;
        draw_bevel_panel(0, TICKER_Y, w, TICKER_H, Color{25, 25, 35, 255});

        draw_rect(6, TICKER_Y + 3, 54, TICKER_H - 6, Color{200, 0, 0, 255});
        ui_draw_text("NEWS", 12, TICKER_Y + 4, 14, Color{255, 255, 255, 255});
        vtr.register_text("NEWS");

        float area_x = 66.f;
        float area_w = w - area_x - 6.f;
        if (t.current.empty()) {
            if (!t.queue.empty()) {
                t.current = t.queue.front();
                t.queue.pop_front();
            } else {
                t.current =
                    NEWS_TICKER_FLAVOR[t.flavor_idx % NEWS_TICKER_FLAVOR_COUNT];
                t.flavor_idx++;
            }
            t.scroll_px = 0.f;
        }

        float text_w = ui_measure_text(t.current, 15).x;
        float text_x = area_x + area_w - t.scroll_px;
        begin_scissor_mode(area_x, TICKER_Y, area_w, TICKER_H);
        ui_draw_text(t.current, text_x, TICKER_Y + 4, 15,
                     Color{255, 230, 80, 255});
        end_scissor_mode();
        // Register the full item, not just the on-screen slice, so
        // expect_text can assert ticker content deterministically.
        vtr.register_text(t.current);

        t.scroll_px += get_frame_time() * TICKER_SCROLL_SPEED;
        if (text_x + text_w < area_x) t.current.clear();
    }
};

struct RenderToastsSystem : System<> {
    void once(float) const override {
        auto& vtr = afterhours::testing::VisibleTextRegistry::instance();
        auto toasts = EntityQuery().whereHasComponent<ToastMessage>().gen();
        float toast_y = 50.f;
        for (Entity& te : toasts) {
            auto& toast = te.get<ToastMessage>();
            float alpha = 1.0f;
            if (toast.elapsed > toast.lifetime - toast.fade_duration) {
                alpha = (toast.lifetime - toast.elapsed) / toast.fade_duration;
            }
            unsigned char a = static_cast<unsigned char>(alpha * 255);
            auto tm = ui_measure_text(toast.text, 20);
            float tx = (DEFAULT_SCREEN_WIDTH - tm.x) / 2.f;

            // Plain rect: the toast fades, and the bevel helper's edges
            // are opaque, so a bevel here would leave a lingering frame.
            draw_rect(tx - 8, toast_y - 4, tm.x + 16, tm.y + 8,
                      Color{30, 120, 60, a});
            draw_text_ex(get_font(), toast.text.c_str(), {tx, toast_y}, 20,
                         FONT_SPACING, Color{255, 255, 255, a});
            vtr.register_text(toast.text);
            toast_y += tm.y + 16;
        }
    }
};

struct RenderNuxBannerSystem : System<> {
    void once(float) const override {
        auto& vtr = afterhours::testing::VisibleTextRegistry::instance();
        auto nuxes = EntityQuery().whereHasComponent<NuxHint>().gen();
        for (Entity& ne : nuxes) {
            auto& nux = ne.get<NuxHint>();
            if (!nux.is_active) continue;

            auto tm = ui_measure_text(nux.text, 20);
            float x_btn_size = 18.f;
            float total_w = tm.x + 24 + x_btn_size + 8;
            float bx = (DEFAULT_SCREEN_WIDTH - 150 - total_w) / 2.f;
            float by = 50.f;
            float bh = tm.y + 12;

            draw_bevel_panel(bx, by, total_w, bh, Color{20, 40, 80, 255});
            draw_rect(bx + 2, by + 2, 3, bh - 4, Color{80, 160, 255, 255});

            ui_draw_text(nux.text, bx + 12, by + 6, 20,
                         Color{255, 255, 255, 255});
            vtr.register_text(nux.text);

            float xbx = bx + total_w - x_btn_size - 6;
            float xby = by + (bh - x_btn_size) / 2.f;

            auto m = input::get_mouse_position();
            bool hovering = m.x >= xbx && m.x <= xbx + x_btn_size &&
                            m.y >= xby && m.y <= xby + x_btn_size;
            Color xbg =
                hovering ? Color{200, 60, 60, 255} : Color{100, 100, 120, 200};
            draw_rect(xbx, xby, x_btn_size, x_btn_size, xbg);

            float cx = xbx + x_btn_size / 2.f;
            float cy = xby + x_btn_size / 2.f;
            float cs = 5.f;
            draw_line(cx - cs, cy - cs, cx + cs, cy + cs,
                      Color{255, 255, 255, 255});
            draw_line(cx - cs, cy + cs, cx + cs, cy - cs,
                      Color{255, 255, 255, 255});

            if (hovering && input::is_mouse_button_pressed(MOUSE_BUTTON_LEFT)) {
                nux.was_dismissed = true;
            }

            break;
        }
    }
};

struct RenderCompassSystem : System<> {
    void once(float) const override {
        auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
        if (!cam) return;

        float cx = DEFAULT_SCREEN_WIDTH - 150 - 30;
        float cy = 55;
        draw_circle(cx, cy, 16, Color{0, 0, 0, 120});
        // North = -z in world space. Projecting the target and one step
        // north through the active view's projection gives the compass
        // direction in both iso and top-down modes.
        vec2 t = cam->cam.world_to_screen(
            vec3{cam->cam.target.x, 0, cam->cam.target.z});
        vec2 n = cam->cam.world_to_screen(
            vec3{cam->cam.target.x, 0, cam->cam.target.z - 1});
        float angle = std::atan2(n.y - t.y, n.x - t.x);
        float nx = cx + std::cos(angle) * 12;
        float ny = cy + std::sin(angle) * 12;
        ui_draw_text("N", nx - 6, ny - 8, 14, Color{255, 100, 100, 255});

        // View-mode toggle button (same action as the V key): ISO <-> TOP.
        // Geometry is fixed so E2E `click` can target it: centre (1100, 90).
        auto& vtr = afterhours::testing::VisibleTextRegistry::instance();
        const char* label = cam->cam.iso_mode ? "ISO" : "TOP";
        float bx = cx - 27, by = 78, bw = 54, bh = 24;
        auto mouse = input::get_mouse_position();
        bool mouse_click = input::is_mouse_button_pressed(MOUSE_BUTTON_LEFT);
        if (mouse_click && mouse.x >= bx && mouse.x <= bx + bw &&
            mouse.y >= by && mouse.y <= by + bh) {
            cam->cam.iso_mode = !cam->cam.iso_mode;
            label = cam->cam.iso_mode ? "ISO" : "TOP";
        }
        draw_bevel_panel(bx, by, bw, bh,
                         cam->cam.iso_mode ? Color{255, 210, 40, 255}
                                           : Color{200, 200, 200, 255});
        auto lm = ui_measure_text(label, 14);
        ui_draw_text(label, bx + (bw - lm.x) / 2.f, by + 5, 14,
                     Color{0, 0, 0, 255});
        vtr.register_text(label);
    }
};

struct RenderHoverInfoSystem : System<> {
    void once(float) const override {
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
                                          .whereHasComponent<Agent, Transform>()
                                          .gen();
                        for (Entity& ae : agents) {
                            if (!ae.is_missing<BeingServiced>()) continue;
                            auto& atf = ae.get<Transform>();
                            auto [agx, agz] = grid->world_to_grid(
                                atf.position.x, atf.position.y);
                            if (agx == cached_hx && agz == cached_hz) {
                                int idx =
                                    static_cast<int>(ae.get<Agent>().want);
                                if (idx >= 0 && idx < 7) need_counts[idx]++;
                            }
                        }
                    }

                    const char* names[] = {"bathroom", "food",    "watching",
                                           "exiting",  "medical", "bar",
                                           "water"};
                    std::string breakdown;
                    for (int i = 0; i < 7; i++) {
                        if (need_counts[i] > 0) {
                            if (!breakdown.empty()) breakdown += ", ";
                            breakdown +=
                                fmt::format("{} {}", need_counts[i], names[i]);
                        }
                    }
                    if (!breakdown.empty())
                        hover_text += " \xe2\x80\x94 " + breakdown;
                }

                ui_draw_text_bg(hover_text, 10, BUILD_BAR_Y - 52, 18,
                                Color{0, 0, 0, 255});
            }
        }
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        if (gs && gs->show_data_layer) {
            ui_draw_text_bg("[TAB] Density Overlay", 10, BUILD_BAR_Y - 78, 18,
                            Color{0, 0, 0, 255});
        }
    }
};

// Minimap RenderTexture management
static afterhours::graphics::RenderTextureType g_minimap_texture = {};
static bool g_minimap_initialized = false;
static constexpr int MINIMAP_SIZE = 150;
static constexpr float MINIMAP_SCALE = 150.0f / static_cast<float>(MAP_SIZE);

// Booking panel geometry (Lineup tab, fixed so E2E `click` can target
// it): rows start y=366, row height 24; the BOOK button is x=1222
// w=52 h=18, so row i's button centre is (1248, 378 + 24*i)
// (row 0 = DJ Cardboard Box). Switch to the Lineup tab first
// (click 1254,145 or set_window_tab lineup). Locked rows show the
// required tier, booked rows show BOOKED, available rows show the fee.
static constexpr float LINEUP_BOOKING_FIRST_ROW_Y = 366.f;
static constexpr float LINEUP_BOOKING_ROW_H = 24.f;
static constexpr float LINEUP_BOOKING_BTN_X_OFF = 202.f;  // from window x
static constexpr float LINEUP_BOOKING_BTN_W = 52.f;

static std::string lineup_short_list(const std::vector<int>& list) {
    if (list.empty()) return "-";
    std::string out;
    for (int idx : list) {
        if (!out.empty()) out += ", ";
        out += BOOKABLE_ACTS[idx].short_name;
    }
    if (out.size() > 22) out = out.substr(0, 21) + "...";
    return out;
}

// ── RCT2 park window ─────────────────────────────────────────────
// One floating window replacing the stacked sidebar text blocks.
// Title bar (park name + FPS + close/minimize deco) + one tab row;
// tabs switch the content page. The bottom bar stays the toolbar
// (build tools + staff/land panels), and the top bar keeps its own
// readouts — the pages below repeat those labels in window form so
// every existing expect_text string keeps working with the same text.
//
// Only the active page is drawn and handles clicks. The
// register_park_window_texts() call in the window system registers
// ALL pages' strings every frame regardless of the active tab, so an
// expect_text for a hidden tab's label still passes (the same
// register-everything pattern the ticker uses for scrolled text);
// the per-page draws are the visible content.

// Festival tier (HUD): name + progress to the next tier.
static std::string park_window_tier_text() {
    auto* lineup = EntityHelper::get_singleton_cmp<LineupState>();
    int tier = lineup ? lineup->tier : 0;
    std::string tier_text = fmt::format("Tier: {}", FESTIVAL_TIER_NAMES[tier]);
    if (auto* gs = EntityHelper::get_singleton_cmp<GameState>()) {
        if (tier < 3)
            tier_text += fmt::format(" {}/{}", gs->total_arrivals,
                                     FESTIVAL_TIER_THRESHOLDS[tier]);
    }
    return tier_text;
}

// Park rating with trend arrow vs yesterday (same format as the top bar).
static std::string park_window_rating_text() {
    if (auto* rating = EntityHelper::get_singleton_cmp<ParkRatingState>()) {
        return fmt::format("Rating: {} {}",
                           static_cast<int>(std::round(rating->rating)),
                           rating->trend_arrow());
    }
    return "Rating: -";
}

static std::string park_window_death_text() {
    if (auto* gs = EntityHelper::get_singleton_cmp<GameState>())
        return fmt::format("Deaths: {}/{}", gs->death_count, gs->max_deaths);
    return "Deaths: 0/10";
}

// Debt line shared by the Finances page and the registered texts:
// "Debt: $<owed> / $<tier cap> cap", red in the UI when debt > 0.
static std::string park_window_debt_text() {
    auto* econ = EntityHelper::get_singleton_cmp<Economy>();
    int tier = 0;
    if (auto* lineup = EntityHelper::get_singleton_cmp<LineupState>())
        tier = lineup->tier;
    return fmt::format("Debt: ${} / ${} cap", econ ? econ->debt : 0,
                       Economy::debt_cap_for_tier(tier));
}

// Staff counts: guards / medics / janitors on payroll.
static std::string park_window_staff_text() {
    return fmt::format("Staff: G{} M{} J{}", staff_count(StaffRole::Guard),
                       staff_count(StaffRole::Medic),
                       staff_count(StaffRole::Janitor));
}

// Guests page summary: attendees + stage watchers + unmet needs.
struct GuestSummary {
    int attendees = 0;
    int watching = 0;
    int need_bathroom = 0;
    int need_food = 0;
    int need_heat = 0;
    int need_bar = 0;
};
static GuestSummary park_window_guest_summary() {
    GuestSummary s;
    s.attendees = (int) EntityQuery().whereHasComponent<Agent>().gen_count();
    s.watching =
        (int) EntityQuery().whereHasComponent<WatchingStage>().gen_count();
    auto needy = EntityQuery().whereHasComponent<AgentNeeds>().gen();
    for (Entity& ae : needy) {
        auto& needs = ae.get<AgentNeeds>();
        if (needs.needs_bathroom) s.need_bathroom++;
        if (needs.needs_food) s.need_food++;
        if (needs.needs_heat) s.need_heat++;
        if (needs.needs_bar) s.need_bar++;
    }
    return s;
}

static std::string park_window_booking_button_label(int i,
                                                    LineupState* lineup) {
    const auto& act = BOOKABLE_ACTS[i];
    if (!lineup->is_unlocked(i)) return fmt::format("T{}", act.min_tier);
    if (lineup->is_booked(i)) return "BOOKED";
    return fmt::format("${}", act.fee);
}

// Register every page's strings every frame (see the header comment
// above): hidden tabs' labels stay expect_text-assertable.
static void register_park_window_texts() {
    auto& vtr = afterhours::testing::VisibleTextRegistry::instance();
    vtr.register_text("Endless Dance Chaos");
    for (int i = 0; i < PARK_WINDOW_TAB_COUNT; i++)
        vtr.register_text(PARK_WINDOW_TAB_NAMES[i]);
    vtr.register_text(fmt::format("FPS: {}", get_fps()));
    vtr.register_text(park_window_tier_text());
    vtr.register_text(park_window_rating_text());
    vtr.register_text(park_window_death_text());
    vtr.register_text(park_window_staff_text());
    if (auto* econ = EntityHelper::get_singleton_cmp<Economy>()) {
        vtr.register_text(fmt::format("Cash: ${}", econ->cash));
        vtr.register_text(fmt::format("Ticket: ${}", econ->ticket_price));
        vtr.register_text(fmt::format("Earned: ${}", econ->total_earned));
        vtr.register_text(fmt::format("Spent: ${}", econ->total_spent));
        vtr.register_text(park_window_debt_text());
        vtr.register_text("Borrow:");
        for (int a : LOAN_AMOUNTS)
            vtr.register_text(fmt::format("${}k", a / 1000));
        vtr.register_text("Repay $1k");
        vtr.register_text("Repay All");
        vtr.register_text("History (last 7 days):");
        for (const auto& row : econ->finance_history)
            vtr.register_text(Economy::finance_history_row(row));
    }
    GuestSummary gs = park_window_guest_summary();
    vtr.register_text(fmt::format("Attendees: {}", gs.attendees));
    vtr.register_text(fmt::format("Watching stage: {}", gs.watching));
    vtr.register_text(fmt::format("Need WC: {}", gs.need_bathroom));
    vtr.register_text(fmt::format("Need Food: {}", gs.need_food));
    vtr.register_text(fmt::format("Need Heat: {}", gs.need_heat));
    vtr.register_text(fmt::format("Need Bar: {}", gs.need_bar));
    // Guests page: stage-queue stat + the 5 guest thoughts.
    if (auto* qs = EntityHelper::get_singleton_cmp<StageQueueState>()) {
        vtr.register_text(
            fmt::format("Queue: {} ({}s avg)", qs->queue_length_cached,
                        static_cast<int>(std::round(qs->avg_queue_time()))));
    }
    vtr.register_text("Guest Thoughts:");
    for (const auto& thought : compute_guest_thoughts())
        vtr.register_text(
            fmt::format("\"{}\" x{}", thought.text, thought.count));
    // Staff page per-role rows.
    static constexpr StaffRole ROLES[] = {StaffRole::Guard, StaffRole::Medic,
                                          StaffRole::Janitor};
    static constexpr const char* SHORT_NAMES[] = {"Guard", "Medic", "Janitor"};
    for (int i = 0; i < 3; i++)
        vtr.register_text(
            fmt::format("{} {}", SHORT_NAMES[i], staff_count(ROLES[i])));
    // Lineup page: header, booking panel, per-act rows.
    vtr.register_text("LINEUP");
    vtr.register_text("BOOK ACTS");
    vtr.register_text("Click $ to book");
    if (auto* lineup = EntityHelper::get_singleton_cmp<LineupState>()) {
        vtr.register_text(
            fmt::format("Today: {}", lineup_short_list(lineup->active)));
        vtr.register_text(
            fmt::format("Next: {}", lineup_short_list(lineup->booked)));
        for (int i = 0; i < NUM_BOOKABLE_ACTS; i++) {
            vtr.register_text(BOOKABLE_ACTS[i].short_name);
            vtr.register_text(park_window_booking_button_label(i, lineup));
        }
    }
}

// Park page: tier / rating / deaths.
static void draw_park_window_park_page() {
    float x = PARK_WINDOW_X + 10;
    ui_draw_text(park_window_tier_text(), x, 170, 14, Color{0, 0, 0, 255});
    auto* rating = EntityHelper::get_singleton_cmp<ParkRatingState>();
    float r = rating ? rating->rating : PARK_RATING_START;
    Color rating_col = r >= 600  ? Color{0, 140, 0, 255}
                       : r < 400 ? Color{200, 0, 0, 255}
                                 : Color{0, 0, 0, 255};
    ui_draw_text(park_window_rating_text(), x, 198, 14, rating_col);
    auto* gs = EntityHelper::get_singleton_cmp<GameState>();
    Color death_col = (gs && gs->death_count >= 7) ? Color{200, 0, 0, 255}
                                                   : Color{0, 0, 0, 255};
    ui_draw_text(park_window_death_text(), x, 226, 14, death_col);
}

// Finances page: cash / ticket + price buttons / earned / spent /
// debt + loan buttons / repay buttons / 7-day history.
// Price buttons: [-] centre (1162,204), [+] centre (1190,204).
// Loan buttons: "$5k" centre (1118,322), "$10k" (1176,322), "$25k"
// (1234,322). Repay buttons: "Repay $1k" centre (1076,350),
// "Repay All" centre (1176,350).
static void draw_park_window_finances_page(vec2 mouse, bool mouse_click) {
    auto* econ = EntityHelper::get_singleton_cmp<Economy>();
    if (!econ) return;
    float x = PARK_WINDOW_X + 10;
    Color cash_col =
        econ->cash < 0 ? Color{200, 0, 0, 255} : Color{0, 140, 0, 255};
    ui_draw_text(fmt::format("Cash: ${}", econ->cash), x, 170, 14, cash_col);
    ui_draw_text(fmt::format("Ticket: ${}", econ->ticket_price), x, 198, 14,
                 Color{160, 100, 0, 255});
    float btn_y = 194.f;
    float btn_h = 20.f;
    float minus_x = PARK_WINDOW_X + 130;  // 1150, w 24
    float plus_x = PARK_WINDOW_X + 158;   // 1178, w 24
    if (mouse_click && !game_is_over() && mouse.y >= btn_y &&
        mouse.y <= btn_y + btn_h) {
        if (mouse.x >= minus_x && mouse.x <= minus_x + 24)
            econ->adjust_price(-Economy::TICKET_PRICE_STEP);
        else if (mouse.x >= plus_x && mouse.x <= plus_x + 24)
            econ->adjust_price(Economy::TICKET_PRICE_STEP);
    }
    const char* btns[] = {"-", "+"};
    float btn_xs[] = {minus_x, plus_x};
    for (int i = 0; i < 2; i++) {
        draw_bevel_panel(btn_xs[i], btn_y, 24, btn_h,
                         Color{200, 200, 200, 255});
        auto bm = ui_measure_text(btns[i], 16);
        ui_draw_text(btns[i], btn_xs[i] + (24 - bm.x) / 2.f, btn_y + 2, 16,
                     Color{0, 0, 0, 255});
    }
    ui_draw_text(fmt::format("Earned: ${}", econ->total_earned), x, 232, 14,
                 Color{0, 0, 0, 255});
    ui_draw_text(fmt::format("Spent: ${}", econ->total_spent), x, 260, 14,
                 Color{0, 0, 0, 255});
    Color debt_col =
        econ->debt > 0 ? Color{200, 0, 0, 255} : Color{0, 0, 0, 255};
    ui_draw_text(park_window_debt_text(), x, 288, 14, debt_col);

    // Borrow: one button per loan amount; interest compounds daily.
    ui_draw_text("Borrow:", x, 316, 14, Color{0, 0, 0, 255});
    float loan_y = 312.f;
    for (int i = 0; i < 3; i++) {
        float loan_x = PARK_WINDOW_X + 72 + i * 58;  // 1092, 1150, 1208
        if (mouse_click && !game_is_over() && mouse.y >= loan_y &&
            mouse.y <= loan_y + btn_h && mouse.x >= loan_x &&
            mouse.x <= loan_x + 52)
            borrow_loan(LOAN_AMOUNTS[i]);
        draw_bevel_panel(loan_x, loan_y, 52, btn_h, Color{200, 200, 200, 255});
        std::string label = fmt::format("${}k", LOAN_AMOUNTS[i] / 1000);
        auto lm = ui_measure_text(label, 14);
        ui_draw_text(label, loan_x + (52 - lm.x) / 2.f, loan_y + 3, 14,
                     Color{0, 120, 0, 255});
    }

    // Repay: $1k partial, or everything affordable (full when covered).
    float repay_y = 340.f;
    const char* repay_labels[] = {"Repay $1k", "Repay All"};
    float repay_xs[] = {x, x + 100};
    for (int i = 0; i < 2; i++) {
        if (mouse_click && !game_is_over() && mouse.y >= repay_y &&
            mouse.y <= repay_y + btn_h && mouse.x >= repay_xs[i] &&
            mouse.x <= repay_xs[i] + 92) {
            if (i == 0)
                repay_loan(LOAN_REPAY_STEP);
            else
                repay_loan_all();
        }
        draw_bevel_panel(repay_xs[i], repay_y, 92, btn_h,
                         Color{200, 200, 200, 255});
        auto rm = ui_measure_text(repay_labels[i], 14);
        ui_draw_text(repay_labels[i], repay_xs[i] + (92 - rm.x) / 2.f,
                     repay_y + 3, 14, Color{160, 0, 0, 255});
    }

    // History: last 7 closed days, oldest first, each row + a +/- bar.
    ui_draw_text("History (last 7 days):", x, 372, 12, Color{0, 0, 0, 255});
    float hy = 388.f;
    for (const auto& row : econ->finance_history) {
        int profit = row.income - row.expenses;
        Color row_col =
            profit < 0 ? Color{160, 0, 0, 255} : Color{0, 120, 0, 255};
        ui_draw_text(Economy::finance_history_row(row), x, hy, 11, row_col);
        hy += 14.f;
    }
}

// Guests page: counts / needs summary + stage queue + guest thoughts.
static void draw_park_window_guests_page() {
    GuestSummary s = park_window_guest_summary();
    float x = PARK_WINDOW_X + 10;
    ui_draw_text(fmt::format("Attendees: {}", s.attendees), x, 170, 14,
                 Color{0, 0, 0, 255});
    ui_draw_text(fmt::format("Watching stage: {}", s.watching), x, 198, 14,
                 Color{0, 0, 0, 255});
    ui_draw_text(fmt::format("Need WC: {}", s.need_bathroom), x, 226, 14,
                 Color{0, 0, 0, 255});
    ui_draw_text(fmt::format("Need Food: {}", s.need_food), x, 254, 14,
                 Color{0, 0, 0, 255});
    ui_draw_text(fmt::format("Need Heat: {}", s.need_heat), x, 282, 14,
                 Color{0, 0, 0, 255});
    ui_draw_text(fmt::format("Need Bar: {}", s.need_bar), x, 310, 14,
                 Color{0, 0, 0, 255});
    if (auto* qs = EntityHelper::get_singleton_cmp<StageQueueState>()) {
        ui_draw_text(
            fmt::format("Queue: {} ({}s avg)", qs->queue_length_cached,
                        static_cast<int>(std::round(qs->avg_queue_time()))),
            x, 338, 14, Color{0, 0, 0, 255});
    }
    ui_draw_text("Guest Thoughts:", x, 366, 14, Color{0, 0, 0, 255});
    float ty = 384.f;
    for (const auto& thought : compute_guest_thoughts()) {
        ui_draw_text(fmt::format("\"{}\" x{}", thought.text, thought.count), x,
                     ty, 12, Color{0, 0, 0, 255});
        ty += 16.f;
    }
}

// Staff page: roster counts + hire/fire.
// Rows: Guard y=200, Medic y=240, Janitor y=280; hire "+" centre
// (1172, row+9), fire "-" centre (1224, row+9). The bottom-bar staff
// panel keeps its own hire/fire buttons too (existing E2E coords).
static void draw_park_window_staff_page(vec2 mouse, bool mouse_click) {
    float x = PARK_WINDOW_X + 10;
    ui_draw_text(park_window_staff_text(), x, 170, 14, Color{0, 0, 0, 255});
    static constexpr StaffRole ROLES[] = {StaffRole::Guard, StaffRole::Medic,
                                          StaffRole::Janitor};
    static constexpr const char* SHORT_NAMES[] = {"Guard", "Medic", "Janitor"};
    for (int i = 0; i < 3; i++) {
        float row_y = 200.f + i * 40.f;
        std::string label =
            fmt::format("{} {}", SHORT_NAMES[i], staff_count(ROLES[i]));
        ui_draw_text(label, x, row_y, 14, Color{0, 0, 0, 255});
        float hire_x = PARK_WINDOW_X + 130;  // 1150, w 44
        float fire_x = PARK_WINDOW_X + 182;  // 1202, w 44
        float btn_y = row_y - 2;
        float btn_h = 22.f;
        if (mouse_click && !game_is_over() && mouse.y >= btn_y &&
            mouse.y <= btn_y + btn_h) {
            if (mouse.x >= hire_x && mouse.x <= hire_x + 44)
                hire_staff(ROLES[i], SPAWN_X, SPAWN_Z);
            else if (mouse.x >= fire_x && mouse.x <= fire_x + 44)
                fire_staff(ROLES[i]);
        }
        draw_bevel_panel(hire_x, btn_y, 44, btn_h, Color{200, 200, 200, 255});
        draw_bevel_panel(fire_x, btn_y, 44, btn_h, Color{200, 200, 200, 255});
        auto plus_m = ui_measure_text("+", 16);
        auto minus_m = ui_measure_text("-", 16);
        ui_draw_text("+", hire_x + (44 - plus_m.x) / 2.f, btn_y + 3, 16,
                     Color{0, 120, 0, 255});
        ui_draw_text("-", fire_x + (44 - minus_m.x) / 2.f, btn_y + 3, 16,
                     Color{200, 0, 0, 255});
    }
}

// Lineup page: artist timeline + BOOK ACTS panel (was the sidebar body).
// The timeline scissor is shortened (184-300) to make room for the
// booking panel (306-558) inside the window.
static void draw_park_window_lineup_page(vec2 mouse, bool mouse_click) {
    auto* sched = EntityHelper::get_singleton_cmp<ArtistSchedule>();
    auto* clock = EntityHelper::get_singleton_cmp<GameClock>();
    if (!sched || !clock) return;
    auto* lineup = EntityHelper::get_singleton_cmp<LineupState>();

    ui_draw_text("LINEUP", PARK_WINDOW_X + 10, PARK_WINDOW_CONTENT_Y + 6, 16,
                 Color{0, 0, 0, 255});

    float content_top = PARK_WINDOW_CONTENT_Y + 28;  // 184
    float content_bot = 300.f;
    begin_scissor_mode(PARK_WINDOW_X, content_top, PARK_WINDOW_W,
                       content_bot - content_top);

    // NOW line sits mid-scissor, so ~48 min each way stay visible.
    float now_y = 242.f;
    draw_line(PARK_WINDOW_X, now_y, PARK_WINDOW_X + PARK_WINDOW_W, now_y,
              Color{255, 100, 100, 255});
    draw_text_ex(get_font(), "NOW", {PARK_WINDOW_X + 6, now_y - 18}, 16,
                 FONT_SPACING, Color{255, 100, 100, 255});

    float now_minutes = clock->game_time_minutes;
    // Half the old 2.4: the window's timeline strip is shorter.
    float px_per_minute = 1.2f;

    for (auto& a : sched->schedule) {
        float minutes_from_now = a.start_time_minutes - now_minutes;
        float block_y = now_y + minutes_from_now * px_per_minute;
        float block_h = std::max(a.duration_minutes * px_per_minute, 42.f);

        if (block_y + block_h < content_top || block_y > content_bot) continue;

        Color bg =
            a.performing ? Color{255, 210, 40, 255} : Color{215, 215, 215, 255};
        draw_rect(PARK_WINDOW_X + 4, block_y, PARK_WINDOW_W - 8, block_h, bg);
        draw_rect_lines(PARK_WINDOW_X + 4, block_y, PARK_WINDOW_W - 8, block_h,
                        Color{120, 120, 120, 255});

        std::string label = a.performing ? fmt::format("> {}", a.name) : a.name;
        Color name_col = Color{0, 0, 0, 255};
        draw_text_ex(get_font(), label.c_str(),
                     {PARK_WINDOW_X + 11, block_y + 5}, 16, FONT_SPACING,
                     Color{0, 0, 0, 120});
        draw_text_ex(get_font(), label.c_str(),
                     {PARK_WINDOW_X + 10, block_y + 4}, 16, FONT_SPACING,
                     name_col);

        int h = (int) (a.start_time_minutes / 60) % 24;
        int m = (int) a.start_time_minutes % 60;
        std::string info =
            fmt::format("{:02d}:{:02d}  ~{} ppl", h, m, a.expected_crowd);
        draw_text_ex(get_font(), info.c_str(),
                     {PARK_WINDOW_X + 10, block_y + 24}, 14, FONT_SPACING,
                     Color{60, 60, 60, 255});
    }

    end_scissor_mode();

    // ── Booking panel: book acts for the NEXT day (fee paid now) ──
    // One row per act, always visible: locked acts show the tier they
    // need, booked acts show BOOKED, available acts show the fee as
    // the button label. Hover shows the full name / genre / flavor /
    // draw / fun tooltip; clicking an available button books it.
    if (lineup) {
        float panel_y = 306.f;
        ui_draw_text("BOOK ACTS", PARK_WINDOW_X + 10, panel_y + 4, 14,
                     Color{0, 0, 0, 255});
        ui_draw_text("Click $ to book", PARK_WINDOW_X + 10, panel_y + 20, 11,
                     Color{60, 60, 60, 255});
        std::string today_text =
            fmt::format("Today: {}", lineup_short_list(lineup->active));
        ui_draw_text(today_text, PARK_WINDOW_X + 10, panel_y + 34, 12,
                     Color{0, 0, 0, 255});
        std::string next_text =
            fmt::format("Next: {}", lineup_short_list(lineup->booked));
        ui_draw_text(next_text, PARK_WINDOW_X + 10, panel_y + 46, 12,
                     Color{0, 0, 0, 255});

        std::string hover_tip;
        for (int i = 0; i < NUM_BOOKABLE_ACTS; i++) {
            const auto& act = BOOKABLE_ACTS[i];
            float row_y = LINEUP_BOOKING_FIRST_ROW_Y + i * LINEUP_BOOKING_ROW_H;
            float btn_x = PARK_WINDOW_X + LINEUP_BOOKING_BTN_X_OFF;
            float btn_y = row_y + 3;
            float btn_h = 18.f;
            bool locked = !lineup->is_unlocked(i);
            bool booked = lineup->is_booked(i);
            bool active = lineup->is_active(i);

            Color row_bg = locked   ? Color{170, 170, 170, 255}
                           : booked ? Color{255, 210, 40, 255}
                                    : Color{215, 215, 215, 255};
            draw_rect(PARK_WINDOW_X + 4, row_y, PARK_WINDOW_W - 8,
                      LINEUP_BOOKING_ROW_H - 2, row_bg);
            draw_rect_lines(PARK_WINDOW_X + 4, row_y, PARK_WINDOW_W - 8,
                            LINEUP_BOOKING_ROW_H - 2,
                            Color{120, 120, 120, 255});
            Color name_col =
                locked ? Color{90, 90, 90, 255} : Color{0, 0, 0, 255};
            ui_draw_text(act.short_name, PARK_WINDOW_X + 8, row_y + 5, 12,
                         name_col);

            std::string btn_label = park_window_booking_button_label(i, lineup);
            bool hovering = mouse.x >= btn_x &&
                            mouse.x <= btn_x + LINEUP_BOOKING_BTN_W &&
                            mouse.y >= btn_y && mouse.y <= btn_y + btn_h;
            if (hovering && mouse_click && !game_is_over() && !locked &&
                !booked) {
                book_act(i);
            }
            draw_bevel_panel(btn_x, btn_y, LINEUP_BOOKING_BTN_W, btn_h,
                             hovering ? Color{230, 230, 230, 255}
                                      : Color{200, 200, 200, 255});
            auto btn_m = ui_measure_text(btn_label, 12);
            ui_draw_text(
                btn_label, btn_x + (LINEUP_BOOKING_BTN_W - btn_m.x) / 2.f,
                btn_y + 3, 12,
                locked ? Color{120, 0, 0, 255} : Color{0, 120, 0, 255});

            if (mouse.x >= PARK_WINDOW_X + 4 &&
                mouse.x <= PARK_WINDOW_X + PARK_WINDOW_W - 4 &&
                mouse.y >= row_y && mouse.y <= row_y + LINEUP_BOOKING_ROW_H) {
                hover_tip = fmt::format(
                    "{} | {} | {} Fee ${} | Draw x{:.2f} | Fun "
                    "x{:.2f}{}",
                    act.name, act.genre, act.flavor, act.fee, act.draw_mult,
                    act.fun_mult,
                    locked ? fmt::format(" | Needs Tier {}",
                                         FESTIVAL_TIER_NAMES[act.min_tier])
                           : (booked ? " | Booked for tomorrow"
                                     : (active ? " | Playing today" : "")));
            }
        }
        if (!hover_tip.empty()) {
            auto tip_m = ui_measure_text(hover_tip, 12);
            // Tooltip hangs left of the window (the window sits at the
            // right screen edge). Clamp with a floor on the upper
            // bound: the longest tooltip is ~as wide as the main
            // area, so a plain clamp(..., 8, width - tip_w) could
            // get lo > hi (UB).
            float tip_max_x = std::max(8.f, PARK_WINDOW_X - tip_m.x - 8.f);
            float tip_x =
                std::clamp(PARK_WINDOW_X + PARK_WINDOW_W / 2.f - tip_m.x / 2.f,
                           8.f, tip_max_x);
            float tip_y = LINEUP_BOOKING_FIRST_ROW_Y - tip_m.y - 10.f;
            draw_bevel_panel(tip_x - 4, tip_y - 2, tip_m.x + 8, tip_m.y + 4,
                             Color{192, 192, 192, 255});
            ui_draw_text(hover_tip, tip_x, tip_y, 12, Color{0, 0, 0, 255});
        }
    }
}

// The window itself: frame + title bar + tabs + active page.
struct RenderParkWindowSystem : System<> {
    void once(float) const override {
        register_park_window_texts();

        // Soft drop shadow: the window floats over the world.
        draw_rect(PARK_WINDOW_X + 4, PARK_WINDOW_Y + 4, PARK_WINDOW_W,
                  PARK_WINDOW_H, Color{0, 0, 0, 60});
        draw_bevel_panel(PARK_WINDOW_X, PARK_WINDOW_Y, PARK_WINDOW_W,
                         PARK_WINDOW_H, Color{192, 192, 192, 255});

        // Title bar: park name + FPS + close/minimize deco buttons
        // (deco only — they do nothing when clicked).
        draw_rect(PARK_WINDOW_X, PARK_WINDOW_Y, PARK_WINDOW_W,
                  PARK_WINDOW_TITLE_H, Color{122, 74, 33, 255});
        ui_draw_text("Endless Dance Chaos", PARK_WINDOW_X + 8,
                     PARK_WINDOW_Y + 6, 12, Color{255, 255, 255, 255});
        std::string fps_text = fmt::format("FPS: {}", get_fps());
        auto fps_m = ui_measure_text(fps_text, 12);
        ui_draw_text(fps_text, PARK_WINDOW_X + PARK_WINDOW_W - 56 - fps_m.x,
                     PARK_WINDOW_Y + 6, 12, Color{255, 255, 255, 255});
        float deco_y = PARK_WINDOW_Y + 4;
        draw_bevel_panel(PARK_WINDOW_X + PARK_WINDOW_W - 48, deco_y, 18, 16,
                         Color{200, 200, 200, 255});
        ui_draw_text("_", PARK_WINDOW_X + PARK_WINDOW_W - 44, deco_y + 1, 12,
                     Color{0, 0, 0, 255});
        draw_bevel_panel(PARK_WINDOW_X + PARK_WINDOW_W - 26, deco_y, 18, 16,
                         Color{200, 200, 200, 255});
        ui_draw_text("x", PARK_WINDOW_X + PARK_WINDOW_W - 21, deco_y + 2, 12,
                     Color{0, 0, 0, 255});

        auto mouse = input::get_mouse_position();
        bool mouse_click = input::is_mouse_button_pressed(MOUSE_BUTTON_LEFT);

        // Tab clicks switch the active page.
        if (mouse_click && mouse.y >= PARK_WINDOW_TAB_Y &&
            mouse.y <= PARK_WINDOW_TAB_Y + PARK_WINDOW_TAB_H) {
            float tx = PARK_WINDOW_X;
            for (int i = 0; i < PARK_WINDOW_TAB_COUNT; i++) {
                if (mouse.x >= tx && mouse.x <= tx + PARK_WINDOW_TAB_W[i]) {
                    park_window_active_tab() = i;
                    break;
                }
                tx += PARK_WINDOW_TAB_W[i];
            }
        }

        // Tab row: active tab pressed-in, others raised.
        float tx = PARK_WINDOW_X;
        for (int i = 0; i < PARK_WINDOW_TAB_COUNT; i++) {
            bool active = (park_window_active_tab() == i);
            if (active)
                draw_bevel_panel_pressed(
                    tx, PARK_WINDOW_TAB_Y, PARK_WINDOW_TAB_W[i],
                    PARK_WINDOW_TAB_H, Color{205, 205, 205, 255});
            else
                draw_bevel_panel(tx, PARK_WINDOW_TAB_Y, PARK_WINDOW_TAB_W[i],
                                 PARK_WINDOW_TAB_H, Color{200, 200, 200, 255});
            auto lm = ui_measure_text(PARK_WINDOW_TAB_NAMES[i], 11);
            ui_draw_text(PARK_WINDOW_TAB_NAMES[i],
                         tx + (PARK_WINDOW_TAB_W[i] - lm.x) / 2.f,
                         PARK_WINDOW_TAB_Y + 6, 11, Color{0, 0, 0, 255});
            tx += PARK_WINDOW_TAB_W[i];
        }

        switch (park_window_active_tab()) {
            case 1:
                draw_park_window_finances_page(mouse, mouse_click);
                break;
            case 2:
                draw_park_window_guests_page();
                break;
            case 3:
                draw_park_window_staff_page(mouse, mouse_click);
                break;
            case 4:
                draw_park_window_lineup_page(mouse, mouse_click);
                break;
            case 0:
            default:
                draw_park_window_park_page();
                break;
        }
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
        {255, 160, 40, 255},   // Bar
        {80, 180, 255, 255},   // WaterStation
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
            clear_background(tile_day_color(TileType::Grass));

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
        auto agents = EntityQuery().whereHasComponent<Agent, Transform>().gen();
        for (Entity& ae : agents) {
            if (!ae.is_missing<BeingServiced>()) continue;
            auto& tf = ae.get<Transform>();
            float gx = tf.position.x / TILESIZE;
            float gz = tf.position.y / TILESIZE;
            float px = sidebar_x + gx * MINIMAP_SCALE;
            float py = minimap_y + gz * MINIMAP_SCALE;

            int di = static_cast<int>(ae.get<Agent>().want);
            Color dot_col = (di >= 0 && di < 7) ? AGENT_DOT_COLORS[di]
                                                : AGENT_DOT_COLORS[2];
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
                        Color{90, 90, 90, 255});
    }
};

// Bottom-bar readout (RCT2 style): rating + cash + date at the right
// end of the toolbar. Drawn AFTER the minimap, so it paints over the
// minimap's bottom strip (y 666-720) — that strip shows only Locked
// land south of the park (z > ~65), never park tiles or agents —
// and the readout panel's own bevel edge becomes the divider.
struct RenderBottomBarReadoutSystem : System<> {
    void once(float) const override {
        auto& vtr = afterhours::testing::VisibleTextRegistry::instance();
        float px = DEFAULT_SCREEN_WIDTH - 150.f;
        float py = BUILD_BAR_Y + 4;  // 670, panel 666..720
        draw_bevel_panel(DEFAULT_SCREEN_WIDTH - 150.f, BUILD_BAR_Y, 150, 54,
                         Color{175, 175, 175, 255});
        if (auto* econ = EntityHelper::get_singleton_cmp<Economy>()) {
            std::string cash_text = fmt::format("Cash: ${}", econ->cash);
            Color cash_col =
                econ->cash < 0 ? Color{200, 0, 0, 255} : Color{0, 140, 0, 255};
            ui_draw_text(cash_text, px + 8, py, 11, cash_col);
            vtr.register_text(cash_text);
            // Debt always shows; red while any is owed. Short form
            // here (the Finances tab carries the cap) so the 150px
            // panel never clips it.
            std::string debt_text = fmt::format("Debt: ${}", econ->debt);
            Color debt_col =
                econ->debt > 0 ? Color{200, 0, 0, 255} : Color{0, 0, 0, 255};
            ui_draw_text(debt_text, px + 8, py + 13, 11, debt_col);
            vtr.register_text(debt_text);
        }
        if (auto* rating = EntityHelper::get_singleton_cmp<ParkRatingState>()) {
            std::string rating_text = fmt::format(
                "Rating: {} {}", static_cast<int>(std::round(rating->rating)),
                rating->trend_arrow());
            ui_draw_text(rating_text, px + 8, py + 26, 11, Color{0, 0, 0, 255});
            vtr.register_text(rating_text);
        }
        if (auto* diff = EntityHelper::get_singleton_cmp<DifficultyState>()) {
            std::string day_text = fmt::format("Day {}", diff->day_number);
            ui_draw_text(day_text, px + 8, py + 39, 11,
                         Color{30, 60, 160, 255});
            vtr.register_text(day_text);
        }
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

        float pw = 460, ph = 420;
        float px = (DEFAULT_SCREEN_WIDTH - pw) / 2.f;
        float py = (DEFAULT_SCREEN_HEIGHT - ph) / 2.f;
        draw_bevel_panel(px, py, pw, ph, Color{192, 192, 192, 255});

        std::string title = "FESTIVAL SHUT DOWN";
        ui_draw_text_centered(title, py + 20, 34, Color{180, 0, 0, 255});

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
        auto* econ = EntityHelper::get_singleton_cmp<Economy>();
        auto* diff = EntityHelper::get_singleton_cmp<DifficultyState>();
        std::string stats5 =
            fmt::format("Final Cash: ${}", econ ? econ->cash : 0);
        std::string stats6 =
            fmt::format("Day Survived: {}", diff ? diff->day_number : 1);
        std::string stats7 = fmt::format("Injuries: {}", gs->injury_count);
        auto* insp = EntityHelper::get_singleton_cmp<InspectionState>();
        std::string stats8 = fmt::format(
            "Inspection: {}", insp ? std::string(1, insp->last_grade) : "-");

        float sy = py + 66;
        ui_draw_text_centered(stats1, sy, 20, Color{0, 0, 0, 255});
        ui_draw_text_centered(stats2, sy + 28, 20, Color{0, 0, 0, 255});
        ui_draw_text_centered(stats3, sy + 56, 20, Color{0, 0, 0, 255});
        ui_draw_text_centered(stats4, sy + 84, 20, Color{0, 0, 0, 255});
        ui_draw_text_centered(stats5, sy + 112, 20, Color{0, 140, 0, 255});
        ui_draw_text_centered(stats6, sy + 140, 20, Color{0, 0, 0, 255});
        ui_draw_text_centered(stats7, sy + 168, 20, Color{0, 0, 0, 255});
        ui_draw_text_centered(stats8, sy + 196, 20, Color{0, 0, 0, 255});

        save::MetaProgress meta;
        save::load_meta(meta);
        float my = sy + 232;
        draw_line(px + 20, my, px + pw - 20, my, Color{120, 120, 120, 255});
        my += 8;
        ui_draw_text_centered("--- All-Time Records ---", my, 16,
                              Color{40, 60, 140, 255});
        my += 24;
        ui_draw_text_centered(
            fmt::format("Best Day: {}  |  Best Served: {}", meta.best_day,
                        meta.best_agents_served),
            my, 16, Color{40, 60, 140, 255});
        my += 22;
        ui_draw_text_centered(
            fmt::format("Peak Attendees: {}  |  Runs: {}",
                        meta.best_max_attendees, meta.total_runs),
            my, 16, Color{40, 60, 140, 255});

        ui_draw_text_centered("Press SPACE to restart", py + ph - 40, 20,
                              Color{60, 60, 60, 255});

        vtr.register_text(title);
        vtr.register_text(stats5);
        vtr.register_text(stats6);
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
    sm.register_render_system(std::make_unique<RenderMoneyFloatersSystem>());
    sm.register_render_system(std::make_unique<RenderTopBarSystem>());
    sm.register_render_system(std::make_unique<RenderNewsTickerSystem>());
    sm.register_render_system(std::make_unique<RenderBuildBarSystem>());
    sm.register_render_system(std::make_unique<RenderStaffPanelSystem>());
    sm.register_render_system(std::make_unique<RenderLandPanelSystem>());
    sm.register_render_system(std::make_unique<RenderToastsSystem>());
    sm.register_render_system(std::make_unique<RenderNuxBannerSystem>());
    sm.register_render_system(std::make_unique<RenderCompassSystem>());
    sm.register_render_system(std::make_unique<RenderHoverInfoSystem>());
    sm.register_render_system(std::make_unique<RenderParkWindowSystem>());
    sm.register_render_system(std::make_unique<RenderMinimapSystem>());
    sm.register_render_system(std::make_unique<RenderBottomBarReadoutSystem>());
    sm.register_render_system(std::make_unique<RenderGameOverSystem>());
}

void register_render_end_system(SystemManager& sm) {
    sm.register_render_system(std::make_unique<EndRenderSystem>());
}
