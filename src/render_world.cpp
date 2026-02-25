// Render domain: 3D world rendering (grid, stage glow, agents, overlays).
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "afterhours/src/core/entity_query.h"
#include "components.h"
#include "gfx3d.h"
#include "render_helpers.h"
#include "systems.h"

static constexpr float LOD_CLOSE_MAX = 25.0f;
static constexpr float LOD_MEDIUM_MAX = 38.0f;

struct BeginRenderSystem : System<> {
    void once(float) const override {
        begin_texture_mode(g_render_texture);
        float nt = get_day_night_t();
        Color bg = lerp_color({40, 44, 52, 255}, {10, 10, 20, 255}, nt);
        clear_background(bg);

        auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
        if (cam) begin_3d(cam->cam.camera);

        auto* vr = EntityHelper::get_singleton_cmp<VisibleRegion>();
        if (!cam || !vr) return;

        float fovy = cam->cam.camera.fovy;
        vr->fovy = fovy;
        if (fovy < LOD_CLOSE_MAX)
            vr->lod = LODLevel::Close;
        else if (fovy < LOD_MEDIUM_MAX)
            vr->lod = LODLevel::Medium;
        else
            vr->lod = LODLevel::Far;

        auto& iso = cam->cam;
        constexpr int MARGIN = 2;
        float sw = static_cast<float>(DEFAULT_SCREEN_WIDTH);
        float sh = static_cast<float>(DEFAULT_SCREEN_HEIGHT);
        vec2 corners[] = {{0, 0}, {sw, 0}, {0, sh}, {sw, sh}};

        bool any_valid = false;
        int gx_min = 0, gx_max = MAP_SIZE - 1;
        int gz_min = 0, gz_max = MAP_SIZE - 1;

        for (auto& c : corners) {
            auto result = iso.screen_to_grid(c.x, c.y);
            if (!result) continue;
            auto [gx, gz] = *result;
            if (!any_valid) {
                gx_min = gx_max = gx;
                gz_min = gz_max = gz;
                any_valid = true;
            } else {
                gx_min = std::min(gx_min, gx);
                gx_max = std::max(gx_max, gx);
                gz_min = std::min(gz_min, gz);
                gz_max = std::max(gz_max, gz);
            }
        }

        if (any_valid) {
            vr->min_x = std::clamp(gx_min - MARGIN, 0, MAP_SIZE - 1);
            vr->max_x = std::clamp(gx_max + MARGIN, 0, MAP_SIZE - 1);
            vr->min_z = std::clamp(gz_min - MARGIN, 0, MAP_SIZE - 1);
            vr->max_z = std::clamp(gz_max + MARGIN, 0, MAP_SIZE - 1);
        } else {
            vr->min_x = 0;
            vr->max_x = MAP_SIZE - 1;
            vr->min_z = 0;
            vr->max_z = MAP_SIZE - 1;
        }
    }
};

struct RenderGridSystem : System<> {
    void once(float) const override {
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;

        auto* vr = EntityHelper::get_singleton_cmp<VisibleRegion>();
        int x0 = vr ? vr->min_x : 0;
        int x1 = vr ? vr->max_x : MAP_SIZE - 1;
        int z0 = vr ? vr->min_z : 0;
        int z1 = vr ? vr->max_z : MAP_SIZE - 1;

        float tile_size = TILESIZE * 0.98f;
        float night_t = get_day_night_t();

        for (int z = z0; z <= z1; z++) {
            for (int x = x0; x <= x1; x++) {
                const Tile& tile = grid->at(x, z);
                Color color = ::tile_color(tile.type, night_t);
                draw_plane({x * TILESIZE, 0.01f, z * TILESIZE},
                           {tile_size, tile_size}, color);
            }
        }
    }
};

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

// Close LOD: individual agent cubes, viewport-culled
static constexpr Color AGENT_PALETTE[] = {
    {212, 165, 116, 255}, {180, 120, 90, 255},  {240, 200, 160, 255},
    {100, 80, 60, 255},   {255, 180, 200, 255}, {100, 200, 255, 255},
    {200, 255, 100, 255}, {255, 220, 100, 255},
};

static constexpr float BODY_W = 0.14f;
static constexpr float BODY_H = 0.32f;
static constexpr float PIP_W = 0.09f;
static constexpr float PIP_H = 0.08f;
static constexpr float SCATTER_RANGE = 0.35f;

struct RenderAgentsSystem : System<> {
    void once(float) const override {
        auto* vr = EntityHelper::get_singleton_cmp<VisibleRegion>();
        if (vr && vr->lod != LODLevel::Close) return;

        auto* grid = EntityHelper::get_singleton_cmp<Grid>();

        auto agents = EntityQuery()
                          .whereHasComponent<Agent>()
                          .whereHasComponent<Transform>()
                          .gen();

        for (Entity& e : agents) {
            if (!e.is_missing<BeingServiced>()) continue;

            auto& agent = e.get<Agent>();
            auto& tf = e.get<Transform>();

            if (vr && grid) {
                auto [gx, gz] =
                    grid->world_to_grid(tf.position.x, tf.position.y);
                if (gx < vr->min_x || gx > vr->max_x || gz < vr->min_z ||
                    gz > vr->max_z)
                    continue;
            }

            float wx = tf.position.x;
            float wz = tf.position.y;

            int eid = static_cast<int>(e.id);
            float ox = hash_scatter(eid * 7 + 3) * SCATTER_RANGE;
            float oz = hash_scatter(eid * 13 + 7) * SCATTER_RANGE;
            wx += ox;
            wz += oz;

            Color body_col = AGENT_PALETTE[agent.color_idx % 8];

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
    }
};

// Medium LOD: per-tile scattered cubes based on agent_count
struct RenderMediumLODSystem : System<> {
    static constexpr int MAX_DOTS_PER_TILE = 8;
    static constexpr float DOT_W = 0.22f;
    static constexpr float DOT_H = 0.25f;
    static constexpr float JITTER_SPEED = 0.5f;
    static constexpr float JITTER_AMOUNT = 0.05f;

    void once(float) const override {
        auto* vr = EntityHelper::get_singleton_cmp<VisibleRegion>();
        if (!vr || vr->lod != LODLevel::Medium) return;

        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;

        float t = get_time();

        for (int z = vr->min_z; z <= vr->max_z; z++) {
            for (int x = vr->min_x; x <= vr->max_x; x++) {
                const Tile& tile = grid->at(x, z);
                if (tile.agent_count <= 0) continue;

                int dots = std::min(tile.agent_count, MAX_DOTS_PER_TILE);
                int tile_seed = x * 1000 + z * 100;

                // Distribute dots proportionally across desires
                int desire_dots[Tile::NUM_DESIRES] = {};
                int assigned = 0;
                for (int d = 0; d < Tile::NUM_DESIRES && assigned < dots; d++) {
                    int dd = tile.desire_counts[d] * dots / tile.agent_count;
                    desire_dots[d] = dd;
                    assigned += dd;
                }
                // Assign remaining dots to the largest desire
                for (int d = 0; d < Tile::NUM_DESIRES && assigned < dots; d++) {
                    if (tile.desire_counts[d] > 0) {
                        desire_dots[d]++;
                        assigned++;
                    }
                }

                int dot_idx = 0;
                for (int d = 0; d < Tile::NUM_DESIRES; d++) {
                    for (int j = 0; j < desire_dots[d]; j++, dot_idx++) {
                        float ox =
                            hash_scatter(tile_seed + dot_idx * 7 + 3) * 0.38f;
                        float oz =
                            hash_scatter(tile_seed + dot_idx * 13 + 7) * 0.38f;

                        float jx =
                            std::sin(t * JITTER_SPEED + dot_idx * 1.7f + x) *
                            JITTER_AMOUNT;
                        float jz = std::cos(t * JITTER_SPEED * 0.8f +
                                            dot_idx * 2.3f + z) *
                                   JITTER_AMOUNT;

                        float wx = x * TILESIZE + ox + jx;
                        float wz = z * TILESIZE + oz + jz;

                        draw_cube({wx, DOT_H * 0.5f, wz}, DOT_W, DOT_H, DOT_W,
                                  DESIRE_COLORS[d]);
                    }
                }
            }
        }
    }
};

// Far LOD: desire-colored blobs that merge across adjacent tiles.
// Color reflects what agents on each tile want (stage=gold, bathroom=teal,
// etc.) Large overlapping disks with neighbor-blended colors create seamless
// merging.
struct RenderFarLODSystem : System<> {
    static constexpr int DISK_SEGMENTS = 16;

    static Color desire_blend(const Tile& tile) {
        if (tile.agent_count <= 0) return {180, 180, 180, 255};
        float inv = 1.0f / tile.agent_count;
        float r = 0, g = 0, b = 0;
        for (int i = 0; i < Tile::NUM_DESIRES; i++) {
            float w = tile.desire_counts[i] * inv;
            r += DESIRE_COLORS[i].r * w;
            g += DESIRE_COLORS[i].g * w;
            b += DESIRE_COLORS[i].b * w;
        }
        return {static_cast<unsigned char>(std::clamp(r, 0.f, 255.f)),
                static_cast<unsigned char>(std::clamp(g, 0.f, 255.f)),
                static_cast<unsigned char>(std::clamp(b, 0.f, 255.f)), 255};
    }

    static Color neighbor_blend(const Grid& grid, int cx, int cz) {
        Color center = desire_blend(grid.at(cx, cz));
        float total_w = static_cast<float>(grid.at(cx, cz).agent_count);
        float r = center.r * total_w;
        float g = center.g * total_w;
        float b = center.b * total_w;

        static constexpr int dx[] = {1, -1, 0, 0, 1, -1, 1, -1};
        static constexpr int dz[] = {0, 0, 1, -1, 1, 1, -1, -1};
        for (int d = 0; d < 8; d++) {
            int nx = cx + dx[d], nz = cz + dz[d];
            if (!grid.in_bounds(nx, nz)) continue;
            const Tile& nb = grid.at(nx, nz);
            if (nb.agent_count <= 0) continue;
            float w = nb.agent_count * (d < 4 ? 0.5f : 0.25f);
            Color nc = desire_blend(nb);
            r += nc.r * w;
            g += nc.g * w;
            b += nc.b * w;
            total_w += w;
        }

        if (total_w < 1.f) return center;
        float inv = 1.0f / total_w;
        return {static_cast<unsigned char>(std::clamp(r * inv, 0.f, 255.f)),
                static_cast<unsigned char>(std::clamp(g * inv, 0.f, 255.f)),
                static_cast<unsigned char>(std::clamp(b * inv, 0.f, 255.f)),
                255};
    }

    static bool has_occupied_neighbor(const Grid& grid, int cx, int cz) {
        static constexpr int dx[] = {1, -1, 0, 0};
        static constexpr int dz[] = {0, 0, 1, -1};
        for (int d = 0; d < 4; d++) {
            int nx = cx + dx[d], nz = cz + dz[d];
            if (grid.in_bounds(nx, nz) && grid.at(nx, nz).agent_count > 0)
                return true;
        }
        return false;
    }

    static bool has_empty_cardinal(const Grid& grid, int cx, int cz) {
        static constexpr int dx[] = {1, -1, 0, 0};
        static constexpr int dz[] = {0, 0, 1, -1};
        for (int d = 0; d < 4; d++) {
            int nx = cx + dx[d], nz = cz + dz[d];
            if (!grid.in_bounds(nx, nz) || grid.at(nx, nz).agent_count <= 0)
                return true;
        }
        return false;
    }

    static unsigned char scale_alpha(float base_alpha, float opacity) {
        return static_cast<unsigned char>(
            std::clamp(base_alpha * opacity, 0.f, 255.f));
    }

    void once(float) const override {
        auto* vr = EntityHelper::get_singleton_cmp<VisibleRegion>();
        if (!vr || vr->lod == LODLevel::Close) return;

        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        if (gs && gs->show_data_layer) return;

        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;

        // Blob opacity: 0% at full zoom in (fovy=5), 100% at max zoom out
        // (fovy=50). Smooth transition across Medium→Far LOD range.
        constexpr float FOVY_MIN = 5.0f;
        constexpr float FOVY_MAX = 50.0f;
        float opacity =
            std::clamp((vr->fovy - FOVY_MIN) / (FOVY_MAX - FOVY_MIN), 0.f, 1.f);
        if (opacity < 0.01f) return;

        float t = get_time();

        // Pass 1: solid quads that tile seamlessly.
        // Adjacent occupied tiles share edges so the fill is continuous.
        for (int z = vr->min_z; z <= vr->max_z; z++) {
            for (int x = vr->min_x; x <= vr->max_x; x++) {
                const Tile& tile = grid->at(x, z);
                if (tile.agent_count <= 0) continue;

                float density = std::min(
                    tile.agent_count / static_cast<float>(MAX_AGENTS_PER_TILE),
                    1.0f);

                Color c1 = neighbor_blend(*grid, x, z);
                c1.a = scale_alpha(180 + density * 75, opacity);

                draw_plane({x * TILESIZE, 0.019f, z * TILESIZE},
                           {TILESIZE, TILESIZE}, c1);
            }
        }

        // Pass 2: soft edge circles on perimeter tiles (those with at
        // least one empty cardinal neighbor). This softens the blob boundary
        // with an organic rounded falloff instead of hard square edges.
        for (int z = vr->min_z; z <= vr->max_z; z++) {
            for (int x = vr->min_x; x <= vr->max_x; x++) {
                const Tile& tile = grid->at(x, z);
                if (tile.agent_count <= 0) continue;
                if (!has_empty_cardinal(*grid, x, z)) continue;

                float density = std::min(
                    tile.agent_count / static_cast<float>(MAX_AGENTS_PER_TILE),
                    1.0f);

                float phase = hash_scatter(x * 31 + z * 57) * 3.14159f;
                float pulse = (std::sin(t * 1.5f + phase) + 1.0f) * 0.5f;

                float r1 = (0.8f + density * 0.4f) + pulse * 0.08f;
                Color c1 = neighbor_blend(*grid, x, z);
                c1.a = scale_alpha(100 + density * 80, opacity);

                draw_cylinder({x * TILESIZE, 0.020f, z * TILESIZE}, r1, r1,
                              0.001f, DISK_SEGMENTS, c1);
            }
        }

        // Pass 3: bright core highlights on dense tiles for visual interest
        for (int z = vr->min_z; z <= vr->max_z; z++) {
            for (int x = vr->min_x; x <= vr->max_x; x++) {
                const Tile& tile = grid->at(x, z);
                if (tile.agent_count < 3) continue;

                float density = std::min(
                    tile.agent_count / static_cast<float>(MAX_AGENTS_PER_TILE),
                    1.0f);
                if (density < 0.15f) continue;

                float phase = hash_scatter(x * 31 + z * 57) * 3.14159f;
                float pulse = (std::sin(t * 2.5f + phase + 2.0f) + 1.0f) * 0.5f;

                float r3 = (0.3f + density * 0.3f) + pulse * 0.03f;
                Color c3 = desire_blend(tile);
                c3.r = static_cast<unsigned char>(
                    std::min(255, static_cast<int>(c3.r) + 40));
                c3.g = static_cast<unsigned char>(
                    std::min(255, static_cast<int>(c3.g) + 40));
                c3.b = static_cast<unsigned char>(
                    std::min(255, static_cast<int>(c3.b) + 40));
                c3.a = scale_alpha(200 + density * 55, opacity);

                draw_cylinder({x * TILESIZE, 0.022f, z * TILESIZE}, r3, r3,
                              0.001f, DISK_SEGMENTS, c3);
            }
        }
    }
};

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

// Merged density system: handles both TAB-toggle heat map and always-on danger
// flash. Uses VisibleRegion for culling.
struct RenderDensitySystem : System<> {
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
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;

        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        bool show_overlay = gs && gs->show_data_layer;

        auto* vr = EntityHelper::get_singleton_cmp<VisibleRegion>();
        int x0 = vr ? vr->min_x : 0;
        int x1 = vr ? vr->max_x : MAP_SIZE - 1;
        int z0 = vr ? vr->min_z : 0;
        int z1 = vr ? vr->max_z : MAP_SIZE - 1;

        float t = get_time();
        float tile_size = TILESIZE * 0.98f;
        int danger_threshold =
            static_cast<int>(DENSITY_DANGEROUS * MAX_AGENTS_PER_TILE);

        for (int z = z0; z <= z1; z++) {
            for (int x = x0; x <= x1; x++) {
                const Tile& tile = grid->at(x, z);
                if (tile.agent_count <= 0) continue;

                float density =
                    tile.agent_count / static_cast<float>(MAX_AGENTS_PER_TILE);

                // TAB-toggle heat map overlay
                if (show_overlay) {
                    Color color = get_density_color(density);
                    draw_plane({x * TILESIZE, 0.05f, z * TILESIZE},
                               {tile_size, tile_size}, color);
                }

                // Always-on danger flash (>=75% density)
                if (tile.agent_count >= danger_threshold) {
                    bool critical = density >= DENSITY_CRITICAL;
                    float freq = critical ? 3.0f : 1.0f;
                    float pulse = (std::sin(t * freq * 6.283f) + 1.0f) * 0.5f;

                    unsigned char alpha;
                    Color flash_color;
                    if (critical) {
                        alpha = static_cast<unsigned char>(40 + pulse * 100);
                        flash_color = {255, 40, 40, alpha};
                    } else {
                        alpha = static_cast<unsigned char>(pulse * 80);
                        flash_color = {255, 140, 0, alpha};
                    }
                    draw_plane({x * TILESIZE, 0.04f, z * TILESIZE},
                               {tile_size, tile_size}, flash_color);
                }
            }
        }
    }
};

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
    sm.register_render_system(std::make_unique<RenderMediumLODSystem>());
    sm.register_render_system(std::make_unique<RenderFarLODSystem>());
    sm.register_render_system(std::make_unique<RenderDensitySystem>());
    sm.register_render_system(std::make_unique<RenderDeathMarkersSystem>());
    sm.register_render_system(std::make_unique<RenderParticlesSystem>());
    sm.register_render_system(std::make_unique<RenderBuildPreviewSystem>());
    sm.register_render_system(std::make_unique<EndMode3DSystem>());
}
