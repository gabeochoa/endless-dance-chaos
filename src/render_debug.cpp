// Render domain: debug panel with tuning sliders.
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "afterhours/src/core/entity_query.h"
#include "components.h"
#include "render_helpers.h"
#include "systems.h"

// Debug panel with sliders for tuning
struct RenderDebugPanelSystem : System<> {
    static float draw_slider(const std::string& label, float x, float y,
                             float w, float val, float min_val, float max_val) {
        float h = 16.f;
        float knob_w = 10.f;

        raylib::DrawTextEx(get_font(), label.c_str(), {x, y - 20}, 18,
                           FONT_SPACING, raylib::Color{200, 200, 200, 255});

        std::string val_text = fmt::format("{:.2f}", val);
        raylib::DrawTextEx(get_font(), val_text.c_str(), {x + w + 8, y - 2}, 18,
                           FONT_SPACING, raylib::Color{255, 255, 100, 255});

        raylib::DrawRectangle((int) x, (int) y, (int) w, (int) h,
                              raylib::Color{60, 60, 70, 255});

        float t = (val - min_val) / (max_val - min_val);
        raylib::DrawRectangle((int) x, (int) y, (int) (w * t), (int) h,
                              raylib::Color{80, 140, 220, 200});

        float knob_x = x + t * (w - knob_w);
        raylib::DrawRectangle((int) knob_x, (int) (y - 2), (int) knob_w,
                              (int) (h + 4), raylib::WHITE);

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

        float pw = 300;
        float ph = 240;
        float px = 10;
        float py = DEFAULT_SCREEN_HEIGHT - 54 - ph - 10;

        raylib::DrawRectangle((int) px, (int) py, (int) pw, (int) ph,
                              raylib::Color{15, 15, 25, 230});
        raylib::DrawRectangleLines((int) px, (int) py, (int) pw, (int) ph,
                                   raylib::Color{100, 100, 120, 255});

        raylib::DrawTextEx(get_font(), "Debug [`]", {px + 10, py + 8}, 20,
                           FONT_SPACING, raylib::Color{255, 200, 80, 255});

        float sx = px + 16;
        float sw = 200;

        gs->speed_multiplier = draw_slider("Agent Speed", sx, py + 55, sw,
                                           gs->speed_multiplier, 0.1f, 20.f);

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

        if (clock) {
            float cur_mult = clock->speed_multiplier();
            float new_mult = draw_slider("Time Speed", sx, py + 155, sw,
                                         cur_mult, 0.f, 20.f);
            if (new_mult != cur_mult) {
                clock->debug_time_mult = (new_mult < 0.05f) ? 0.f : new_mult;
            }
        }

        int agent_count =
            (int) EntityQuery().whereHasComponent<Agent>().gen_count();
        std::string info =
            fmt::format("Agents: {}  Deaths: {}", agent_count, gs->death_count);
        raylib::DrawTextEx(get_font(), info.c_str(), {sx, py + 200}, 16,
                           FONT_SPACING, raylib::Color{160, 160, 160, 255});
    }
};

void register_render_debug_systems(SystemManager& sm) {
    sm.register_render_system(std::make_unique<RenderDebugPanelSystem>());
}
