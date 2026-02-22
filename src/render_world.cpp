// Render domain: 3D world rendering (grid, stage glow, agents, overlays).
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "afterhours/src/core/entity_query.h"
#include "components.h"
#include "gfx3d.h"
#include "render_helpers.h"
#include "systems.h"

struct BeginRenderSystem : System<> {
    void once(float) const override {
        begin_texture_mode(g_render_texture);
        float nt = get_day_night_t();
        Color bg = lerp_color({40, 44, 52, 255}, {10, 10, 20, 255}, nt);
        clear_background(bg);

        auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
        if (cam) begin_3d(cam->cam.camera);
    }
};

struct RenderGridSystem : System<> {
    void once(float) const override {
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;

        float tile_size = TILESIZE * 0.98f;
        float night_t = get_day_night_t();

        for (int z = 0; z < MAP_SIZE; z++) {
            for (int x = 0; x < MAP_SIZE; x++) {
                const Tile& tile = grid->at(x, z);
                Color color = ::tile_color(tile.type, night_t);
                draw_plane({x * TILESIZE, 0.01f, z * TILESIZE},
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

        if (sched->stage_state == StageState::Announcing) {
            float t = get_time();
            float pulse = (std::sin(t * 3.0f) + 1.0f) * 0.5f;
            auto alpha = static_cast<unsigned char>(40 + pulse * 40);
            Color glow = {255, 200, 50, alpha};
            float ts = TILESIZE * 0.98f;
            for (int z = STAGE_Z; z < STAGE_Z + STAGE_SIZE; z++)
                for (int x = STAGE_X; x < STAGE_X + STAGE_SIZE; x++)
                    draw_plane({x * TILESIZE, 0.06f, z * TILESIZE}, {ts, ts},
                               glow);
            return;
        }

        if (sched->stage_state != StageState::Performing) return;

        float t = get_time();
        float beat_phase = std::fmod(t * (128.0f / 60.0f), 1.0f);
        float pulse = std::pow(std::max(0.0f, 1.0f - beat_phase * 4.0f), 2.0f);

        auto glow_alpha = static_cast<unsigned char>(60 + pulse * 100);
        Color glow = {255, 180, 0, glow_alpha};
        float ts = TILESIZE * 0.98f;
        for (int z = STAGE_Z; z < STAGE_Z + STAGE_SIZE; z++)
            for (int x = STAGE_X; x < STAGE_X + STAGE_SIZE; x++)
                draw_plane({x * TILESIZE, 0.06f, z * TILESIZE}, {ts, ts}, glow);

        float beam_h = 2.5f + pulse * 0.5f;
        float beam_r = 0.06f;
        Color beam_colors[] = {
            {255, 50, 50, 120},
            {50, 50, 255, 120},
            {50, 255, 50, 120},
            {255, 255, 50, 120},
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
            draw_cylinder({corners[i][0], 0.0f, corners[i][1]}, beam_r,
                          beam_r * 0.3f, beam_h, 4, beam_colors[i]);
            draw_sphere(
                {corners[i][0] + sway_x, beam_h, corners[i][1] + sway_z}, 0.08f,
                beam_colors[i]);
        }

        auto spot_alpha = static_cast<unsigned char>(80 + pulse * 80);
        draw_cylinder({stage_cx, 0.0f, stage_cz}, 0.1f, 0.6f + pulse * 0.2f,
                      2.0f + pulse * 0.5f, 6, {255, 255, 200, spot_alpha});
    }
};

// Desire pip colors
static constexpr Color DESIRE_COLORS[] = {
    {126, 207, 192, 255},  // Bathroom
    {244, 164, 164, 255},  // Food
    {255, 217, 61, 255},   // Stage
    {68, 136, 170, 255},   // Exit/Gate
    {255, 100, 100, 255},  // MedTent
};

static float hash_scatter(int seed) {
    uint32_t h = static_cast<uint32_t>(seed);
    h ^= h >> 16;
    h *= 0x45d9f3b;
    h ^= h >> 16;
    return (static_cast<float>(h & 0xFFFF) / 32767.5f) - 1.0f;
}

struct RenderAgentsSystem : System<Agent, Transform> {
    static constexpr Color PALETTE[] = {
        {212, 165, 116, 255}, {180, 120, 90, 255},  {240, 200, 160, 255},
        {100, 80, 60, 255},   {255, 180, 200, 255}, {100, 200, 255, 255},
        {200, 255, 100, 255}, {255, 220, 100, 255},
    };

    static constexpr float BODY_W = 0.14f;
    static constexpr float BODY_H = 0.32f;
    static constexpr float PIP_W = 0.09f;
    static constexpr float PIP_H = 0.08f;
    static constexpr float SCATTER_RANGE = 0.35f;

    void for_each_with(Entity& e, Agent& agent, Transform& tf, float) override {
        if (!e.is_missing<BeingServiced>()) return;

        float wx = tf.position.x;
        float wz = tf.position.y;

        int eid = static_cast<int>(e.id);
        float ox = hash_scatter(eid * 7 + 3) * SCATTER_RANGE;
        float oz = hash_scatter(eid * 13 + 7) * SCATTER_RANGE;
        wx += ox;
        wz += oz;

        Color body_col = PALETTE[agent.color_idx % 8];

        float bob_y = 0.0f;
        if (!e.is_missing<WatchingStage>()) {
            auto& ws = e.get<WatchingStage>();
            bob_y = std::sin(ws.watch_timer * 6.0f) * 0.03f;
        }

        if (!e.is_missing<AgentHealth>()) {
            float hp = e.get<AgentHealth>().hp;
            if (hp < 0.5f) {
                float t = hp / 0.5f;
                body_col.r = static_cast<unsigned char>(body_col.r * t +
                                                        255 * (1.f - t));
                body_col.g = static_cast<unsigned char>(body_col.g * t);
                body_col.b = static_cast<unsigned char>(body_col.b * t);
            }
        }

        float base_y = 0.16f + bob_y;
        draw_cube({wx, base_y, wz}, BODY_W, BODY_H, BODY_W, body_col);

        int desire_idx = static_cast<int>(agent.want);
        Color pip_col = DESIRE_COLORS[desire_idx];
        float pip_y = base_y + BODY_H * 0.5f + PIP_H * 0.5f;
        draw_cube({wx, pip_y, wz}, PIP_W, PIP_H, PIP_W, pip_col);
    }
};

// Render path-drawing preview overlays
struct RenderBuildPreviewSystem : System<> {
    static constexpr Color PREVIEW_VALID = {100, 220, 130, 100};
    static constexpr Color PREVIEW_EXISTING = {180, 180, 180, 80};
    static constexpr Color HOVER_NORMAL = {255, 255, 255, 120};
    static constexpr Color HOVER_DEMOLISH = {255, 60, 60, 140};

    void once(float) const override {
        auto* pds = EntityHelper::get_singleton_cmp<PathDrawState>();
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!pds || !grid || !pds->hover_valid) return;

        float tile_size = TILESIZE * 0.98f;
        float preview_y = 0.03f;

        if (pds->is_drawing) {
            int min_x, min_z, max_x, max_z;
            pds->get_rect(min_x, min_z, max_x, max_z);

            for (int z = min_z; z <= max_z; z++) {
                for (int x = min_x; x <= max_x; x++) {
                    if (!grid->in_bounds(x, z)) continue;
                    bool already_path = grid->at(x, z).type == TileType::Path;
                    Color color =
                        already_path ? PREVIEW_EXISTING : PREVIEW_VALID;
                    draw_plane({x * TILESIZE, preview_y, z * TILESIZE},
                               {tile_size, tile_size}, color);
                }
            }
        }

        Color cursor_color = pds->demolish_mode ? HOVER_DEMOLISH : HOVER_NORMAL;
        draw_plane(
            {pds->hover_x * TILESIZE, preview_y, pds->hover_z * TILESIZE},
            {tile_size, tile_size}, cursor_color);
    }
};

// Density heat map overlay
struct RenderDensityOverlaySystem : System<> {
    static Color get_density_color(float density_ratio) {
        if (density_ratio < 0.50f) {
            float t = density_ratio / 0.50f;
            return Color{255, 255, 0, static_cast<unsigned char>(t * 180)};
        } else if (density_ratio < 0.75f) {
            float t = (density_ratio - 0.50f) / 0.25f;
            return Color{255, static_cast<unsigned char>(255 - t * 90), 0, 180};
        } else if (density_ratio < 0.90f) {
            float t = (density_ratio - 0.75f) / 0.15f;
            return Color{255, static_cast<unsigned char>(165 - t * 165), 0,
                         200};
        } else {
            float t = std::min((density_ratio - 0.90f) / 0.10f, 1.0f);
            return Color{static_cast<unsigned char>(255 - t * 255), 0, 0, 220};
        }
    }

    void once(float) const override {
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        if (!gs || !gs->show_data_layer) return;

        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;

        float tile_size = TILESIZE * 0.98f;
        float overlay_y = 0.05f;

        for (int z = 0; z < MAP_SIZE; z++) {
            for (int x = 0; x < MAP_SIZE; x++) {
                const Tile& tile = grid->at(x, z);
                if (tile.agent_count <= 0) continue;

                float density =
                    tile.agent_count / static_cast<float>(MAX_AGENTS_PER_TILE);
                Color color = get_density_color(density);

                draw_plane({x * TILESIZE, overlay_y, z * TILESIZE},
                           {tile_size, tile_size}, color);
            }
        }
    }
};

// Always-on density warning flash for dangerous tiles (75%+)
struct RenderDensityFlashSystem : System<> {
    void once(float) const override {
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;

        float t = get_time();
        float tile_size = TILESIZE * 0.98f;
        int danger_threshold =
            static_cast<int>(DENSITY_DANGEROUS * MAX_AGENTS_PER_TILE);

        for (int z = 0; z < MAP_SIZE; z++) {
            for (int x = 0; x < MAP_SIZE; x++) {
                const Tile& tile = grid->at(x, z);
                if (tile.agent_count < danger_threshold) continue;

                float density =
                    tile.agent_count / static_cast<float>(MAX_AGENTS_PER_TILE);
                bool critical = density >= DENSITY_CRITICAL;

                float freq = critical ? 3.0f : 1.0f;
                float pulse = (std::sin(t * freq * 6.283f) + 1.0f) * 0.5f;

                unsigned char alpha;
                Color color;
                if (critical) {
                    alpha = static_cast<unsigned char>(40 + pulse * 100);
                    color = {255, 40, 40, alpha};
                } else {
                    alpha = static_cast<unsigned char>(pulse * 80);
                    color = {255, 140, 0, alpha};
                }

                draw_plane({x * TILESIZE, 0.04f, z * TILESIZE},
                           {tile_size, tile_size}, color);
            }
        }
    }
};

// Render death location markers as red X shapes
struct RenderDeathMarkersSystem : System<DeathMarker> {
    void for_each_with(Entity&, DeathMarker& dm, float) override {
        float alpha_f = 1.0f;
        float fade_start = 3.0f;
        if (dm.lifetime < fade_start) alpha_f = dm.lifetime / fade_start;

        auto alpha = static_cast<unsigned char>(alpha_f * 255.f);
        Color color = {255, 60, 60, alpha};
        float s = 0.15f;
        float y = 0.08f;
        float wx = dm.position.x;
        float wz = dm.position.y;

        draw_line_3d({wx - s, y, wz - s}, {wx + s, y, wz + s}, color);
        draw_line_3d({wx - s, y, wz + s}, {wx + s, y, wz - s}, color);
    }
};

// Render death particles as small 3D cubes
struct RenderParticlesSystem : System<Particle, Transform> {
    void for_each_with(Entity&, Particle& p, Transform& tf, float) override {
        float wx = tf.position.x;
        float wz = tf.position.y;
        float s = p.size * 0.02f;

        float life_t = 1.0f - (p.lifetime / p.max_lifetime);
        float y = 0.1f + life_t * 0.5f;

        draw_cube({wx, y, wz}, s, s, s, p.color);
    }
};

struct EndMode3DSystem : System<> {
    void once(float) const override {
        auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
        if (cam) end_3d();
    }
};

void register_render_world_systems(SystemManager& sm) {
    sm.register_render_system(std::make_unique<BeginRenderSystem>());
    sm.register_render_system(std::make_unique<RenderGridSystem>());
    sm.register_render_system(std::make_unique<RenderStageGlowSystem>());
    sm.register_render_system(std::make_unique<RenderAgentsSystem>());
    sm.register_render_system(std::make_unique<RenderDensityFlashSystem>());
    sm.register_render_system(std::make_unique<RenderDensityOverlaySystem>());
    sm.register_render_system(std::make_unique<RenderDeathMarkersSystem>());
    sm.register_render_system(std::make_unique<RenderParticlesSystem>());
    sm.register_render_system(std::make_unique<RenderBuildPreviewSystem>());
    sm.register_render_system(std::make_unique<EndMode3DSystem>());
}
