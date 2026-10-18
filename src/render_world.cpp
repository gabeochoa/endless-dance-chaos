// Render domain: 3D world rendering (grid, stage glow, agents, overlays).
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "afterhours/src/core/entity_query.h"
#include "afterhours/src/plugins/e2e_testing/visible_text.h"
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
        clear_background(sky_color(nt));

        auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
        // Iso mode draws pure 2D diamonds into the texture: no 3D
        // camera, no begin_3d. The legacy 3D path keeps begin_3d.
        if (cam && !cam->cam.iso_mode) begin_3d(cam->cam.camera);

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
        // In iso mode, cull to the world viewport (top bar / build bar /
        // sidebar excluded): the full-screen corners would needlessly
        // cover the whole map's AABB.
        vec2 corners[] = {{0, 0}, {sw, 0}, {0, sh}, {sw, sh}};
        if (iso.iso_mode) {
            vec2 wc[] = {{0, 44}, {1130, 44}, {0, 644}, {1130, 644}};
            for (int i = 0; i < 4; i++) corners[i] = wc[i];
        }

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

// Desire pip colors (indexed by FacilityType) — match the saturated
// building palette so a pip reads as its destination at a glance.
static constexpr Color DESIRE_COLORS[] = {
    {0, 210, 200, 255},    // Bathroom
    {255, 80, 160, 255},   // Food
    {200, 30, 230, 255},   // Stage
    {255, 255, 255, 255},  // Exit/Gate
    {255, 45, 45, 255},    // MedTent
    {255, 145, 0, 255},    // Bar
    {0, 180, 255, 255},    // WaterStation
};

static float hash_scatter(int seed) {
    uint32_t h = static_cast<uint32_t>(seed);
    h ^= h >> 16;
    h *= 0x45d9f3b;
    h ^= h >> 16;
    return (static_cast<float>(h & 0xFFFF) / 32767.5f) - 1.0f;
}

// Bright saturated shirt colors, picked by a hash of the entity id mixed
// with the spawn-time color index, so neighbors rarely match.
static constexpr Color AGENT_SHIRT_PALETTE[] = {
    {255, 60, 60, 255},  {255, 150, 0, 255},  {255, 225, 0, 255},
    {70, 215, 90, 255},  {0, 200, 255, 255},  {90, 120, 255, 255},
    {255, 90, 200, 255}, {180, 90, 255, 255},
};

static uint32_t hash_agent_color(int eid, uint8_t color_idx) {
    uint32_t h = static_cast<uint32_t>(eid) * 2654435761u ^
                 static_cast<uint32_t>(color_idx) * 40503u;
    h ^= h >> 16;
    h *= 0x45d9f3b;
    h ^= h >> 16;
    return h;
}

static constexpr float BODY_W = 0.14f;
static constexpr float BODY_H = 0.32f;
static constexpr float PIP_W = 0.09f;
static constexpr float PIP_H = 0.08f;
static constexpr float SCATTER_RANGE = 0.35f;

// Density heat-map color (yellow -> orange -> red -> dark red). Same
// stops as RenderDensitySystem::get_density_color below; duplicated here
// because the iso pass is defined above that system and must stay a
// single sorted draw list.
static Color iso_density_color(float density_ratio) {
    if (density_ratio < 0.50f) {
        float t = density_ratio / 0.50f;
        return Color{255, 255, 0, static_cast<unsigned char>(t * 180)};
    } else if (density_ratio < 0.75f) {
        float t = (density_ratio - 0.50f) / 0.25f;
        return Color{255, static_cast<unsigned char>(255 - t * 90), 0, 180};
    } else if (density_ratio < 0.90f) {
        float t = (density_ratio - 0.75f) / 0.15f;
        return Color{255, static_cast<unsigned char>(165 - t * 165), 0, 200};
    } else {
        float t = std::min((density_ratio - 0.90f) / 0.10f, 1.0f);
        return Color{static_cast<unsigned char>(255 - t * 255), 0, 0, 220};
    }
}

// Forward: defined below with the legacy 3D grid system.
static bool is_building_tile(TileType type);

// ── 2D isometric renderer ────────────────────────────────────────────
// One sorted draw list (painter's algorithm): every tile, building,
// peep, overlay, particle and marker becomes an item keyed by depth
// (x+z, continuous for agents) + a layer order, then items draw
// back-to-front so nearer objects correctly overlap farther ones.
// Tiles/buildings are filled quads: on raylib, RL_TRIANGLES does not
// rasterize in this render-texture pass (verified with a lone
// DrawTriangle: background untouched, while RL_QUADS draws), so quads
// go through afterhours draw_quad there. On Metal, draw_quad is only a
// bounding box, so quads are split into two triangles (sgl handles
// them) — iso_draw_poly4 hides the split.

// Fill a convex quad (a, b, c, d) in order.
static void iso_draw_poly4(vec2 a, vec2 b, vec2 c, vec2 d, Color color) {
#ifdef AFTER_HOURS_USE_RAYLIB
    vec2 q[4] = {a, b, c, d};
    draw_quad(q, color);
#else
    draw_triangle(a, b, c, color);
    draw_triangle(a, c, d, color);
#endif
}

static void iso_draw_diamond(float cx, float cy, float tw, float th,
                             Color color) {
    vec2 top{cx, cy - th * 0.5f};
    vec2 right{cx + tw * 0.5f, cy};
    vec2 bottom{cx, cy + th * 0.5f};
    vec2 left{cx - tw * 0.5f, cy};
    iso_draw_poly4(top, right, bottom, left, color);
}

// Ground tile with a dark seam: a full-size darkened diamond underneath
// a slightly inset tile-color diamond reads as a 1px grid line without
// a per-tile line call (cheaper, and batch-friendly).
static void iso_draw_tile(float cx, float cy, float tw, float th, Color color) {
    iso_draw_diamond(cx, cy, tw, th, darken_color(color, 0.35f));
    iso_draw_diamond(cx, cy, std::max(tw - 2.0f, 1.0f),
                     std::max(th - 1.0f, 1.0f), color);
}

// Raised terrain tile: side skirts from the base diamond up to the
// lifted diamond, so a +1 plot reads as a step, not a floating tile.
// Left skirt darker, right skirt lighter (same shading as iso blocks).
static void iso_draw_raised_tile(float cx, float cy, float tw, float th,
                                 Color color, float h) {
    float ry = cy - h;  // raised-diamond center
    vec2 right{cx + tw * 0.5f, ry};
    vec2 bottom{cx, ry + th * 0.5f};
    vec2 left{cx - tw * 0.5f, ry};
    vec2 bottom_base{cx, cy + th * 0.5f};
    vec2 left_base{cx - tw * 0.5f, cy};
    vec2 right_base{cx + tw * 0.5f, cy};
    iso_draw_poly4(left, bottom, bottom_base, left_base,
                   darken_color(color, 0.35f));
    iso_draw_poly4(right, bottom, bottom_base, right_base,
                   darken_color(color, 0.15f));
    iso_draw_tile(cx, ry, tw, th, color);
}

// Extruded block: two side faces + a lightened roof diamond. Height is in
// screen px (see RenderIsoSystem for the per-type heights).
static void iso_draw_block(float cx, float cy, float tw, float th, float h,
                           Color color) {
    float ry = cy - h;  // roof-center height above the ground center
    vec2 top{cx, ry - th * 0.5f};
    vec2 right{cx + tw * 0.5f, ry};
    vec2 bottom{cx, ry + th * 0.5f};
    vec2 left{cx - tw * 0.5f, ry};
    vec2 bottom2{cx, cy + th * 0.5f};
    vec2 left2{cx - tw * 0.5f, cy};
    vec2 right2{cx + tw * 0.5f, cy};
    // Left face (darker) and right face, then the roof on top.
    Color left_c = darken_color(color, 0.35f);
    Color right_c = darken_color(color, 0.15f);
    iso_draw_poly4(left, bottom, bottom2, left2, left_c);
    iso_draw_poly4(right, bottom2, bottom, right2, right_c);
    iso_draw_diamond(cx, ry, tw, th, lighten_color(color, 0.40f));
}

// Urgent-need glyph for a thought bubble: the highest need ratio wins;
// heat breaks ties (it can kill). Returns nullptr below the 85% line.
static const char* iso_urgent_glyph(const AgentNeeds* needs) {
    if (!needs) return nullptr;
    auto ratio = [](float timer, float threshold) {
        return threshold > 0.f ? timer / threshold : 0.f;
    };
    float heat = ratio(needs->heat_timer, needs->heat_threshold);
    float wc = ratio(needs->bathroom_timer, needs->bathroom_threshold);
    float food = ratio(needs->food_timer, needs->food_threshold);
    float bar = ratio(needs->bar_timer, needs->bar_threshold);
    float best = std::max(std::max(heat, wc), std::max(food, bar));
    if (best <= 0.85f) return nullptr;
    if (heat >= best) return "HEAT";
    if (wc >= best) return "WC";
    if (food >= best) return "FOOD";
    return "BAR";
}

struct IsoPeep {
    float wx = 0, wz = 0, bob = 0;
    Color body{255, 255, 255, 255};
    Color pip{255, 255, 255, 255};
    const char* glyph = nullptr;  // thought-bubble glyph, or nullptr
    bool is_staff = false;
    bool is_vip = false;  // gold body + star bubble, not a need glyph
    // Screen-px lift from the terrain height of the tile underfoot:
    // visual only, movement stays flat.
    float lift = 0;
};

struct IsoPoint {
    float wx = 0, wz = 0, lift = 0, size = 0;
    Color color{255, 255, 255, 255};
    int shape = 0;  // 0 = particle rect, 1 = death-marker X
};

// One entry in the painter's draw list.
struct IsoDraw {
    float depth = 0;  // x+z; continuous for agents
    int order = 0;    // tile 0, overlay 1, building 2, peep 3, bubble 4, fx 5
    // kind: 0 tile, 1 overlay, 2 building, 3 peep, 4 bubble, 5 fx,
    // 6 tree, 7 lamp
    int kind = 0;
    int x = 0, z = 0;
    Color color{255, 255, 255, 255};
    float h = 0;   // building height px
    int ref = -1;  // IsoPeep / IsoPoint snapshot index
    // Screen-px terrain lift for tiles/buildings/trees/lamps on
    // raised plots (Tile::height * iso_tile_h * 0.35).
    float lift = 0;
};

struct RenderIsoSystem : System<> {
    void once(float) const override {
        auto* cam_cmp = EntityHelper::get_singleton_cmp<ProvidesCamera>();
        if (!cam_cmp || !cam_cmp->cam.iso_mode) return;
        auto& cam = cam_cmp->cam;
        float tw = cam.iso_tile_w();
        float th = cam.iso_tile_h();

        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;
        auto* vr = EntityHelper::get_singleton_cmp<VisibleRegion>();
        int x0 = vr ? vr->min_x : 0;
        int x1 = vr ? vr->max_x : MAP_SIZE - 1;
        int z0 = vr ? vr->min_z : 0;
        int z1 = vr ? vr->max_z : MAP_SIZE - 1;
        float night_t = get_day_night_t();
        float t = get_time();
        auto weather = atmosphere_weather();
        float shimmer =
            weather.heat ? HEAT_SHIMMER_AMPLITUDE * std::sin(t * 3.0f) : 0.f;

        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        bool show_overlay = gs && gs->show_data_layer;
        int danger_threshold =
            static_cast<int>(DENSITY_DANGEROUS * MAX_AGENTS_PER_TILE);

        auto* sched = EntityHelper::get_singleton_cmp<ArtistSchedule>();
        bool stage_glow =
            sched && (sched->stage_state == StageState::Announcing ||
                      sched->stage_state == StageState::Performing);
        float glow_alpha = 0;
        Color glow_color{255, 180, 0, 0};
        if (stage_glow) {
            if (sched->stage_state == StageState::Announcing) {
                float pulse = (std::sin(t * 3.0f) + 1.0f) * 0.5f;
                glow_alpha = 40 + pulse * 40;
                glow_color = {255, 200, 50,
                              static_cast<unsigned char>(glow_alpha)};
            } else {
                float beat_phase = std::fmod(t * (128.0f / 60.0f), 1.0f);
                float pulse =
                    std::pow(std::max(0.0f, 1.0f - beat_phase * 4.0f), 2.0f);
                glow_alpha = 60 + pulse * 100;
                glow_color = {255, 180, 0,
                              static_cast<unsigned char>(glow_alpha)};
            }
        }

        std::vector<IsoDraw> items;
        items.reserve((x1 - x0 + 1) * (z1 - z0 + 1) * 2 + 512);
        std::vector<IsoPeep> peeps;
        std::vector<IsoPoint> points;

        // Tiles (+ building extrusions + ground overlays).
        for (int z = z0; z <= z1; z++) {
            for (int x = x0; x <= x1; x++) {
                const Tile& tile = grid->at(x, z);
                float depth = static_cast<float>(x + z);
                // Raised-plot lift: one height step = 0.35 tile-heights.
                float lift = tile.height * th * 0.35f;
                Color weather_color =
                    ::tile_color_weather(tile.type, night_t, weather, shimmer);
                items.push_back(
                    {depth, 0, 0, x, z, weather_color, 0, -1, lift});
                if (tile.type == TileType::Fence) {
                    // Low extruded block so the fence ring reads as a wall.
                    items.push_back({depth, 2, 2, x, z, weather_color,
                                     th * 0.35f, -1, lift});
                } else if (tile.type == TileType::Tree) {
                    // Green blob-tree: canopy ellipse + trunk (kind 6).
                    items.push_back(
                        {depth, 2, 6, x, z, weather_color, 0, -1, lift});
                } else if (tile.type == TileType::Lamp) {
                    // Lamp post + bulb (kind 7), drawn from the tile
                    // color so it tints with day/night.
                    items.push_back(
                        {depth, 2, 7, x, z, weather_color, 0, -1, lift});
                } else if (is_building_tile(tile.type)) {
                    float h = tile.type == TileType::Stage   ? th * 0.95f
                              : tile.type == TileType::Bench ? th * 0.25f
                                                             : th * 0.55f;
                    items.push_back(
                        {depth, 2, 2, x, z, weather_color, h, -1, lift});
                }
                if (stage_glow && tile.type == TileType::Stage) {
                    items.push_back(
                        {depth, 1, 1, x, z, glow_color, 0, -1, lift});
                }
                if (tile.agent_count > 0) {
                    if (show_overlay) {
                        float density = tile.agent_count /
                                        static_cast<float>(MAX_AGENTS_PER_TILE);
                        items.push_back({depth, 1, 1, x, z,
                                         iso_density_color(density), 0, -1,
                                         lift});
                    }
                    if (tile.agent_count >= danger_threshold) {
                        bool critical =
                            tile.agent_count /
                                static_cast<float>(MAX_AGENTS_PER_TILE) >=
                            DENSITY_CRITICAL;
                        float freq = critical ? 3.0f : 1.0f;
                        float pulse =
                            (std::sin(t * freq * 6.283f) + 1.0f) * 0.5f;
                        unsigned char alpha;
                        Color flash;
                        if (critical) {
                            alpha =
                                static_cast<unsigned char>(40 + pulse * 100);
                            flash = {255, 40, 40, alpha};
                        } else {
                            alpha = static_cast<unsigned char>(pulse * 80);
                            flash = {255, 140, 0, alpha};
                        }
                        items.push_back(
                            {depth, 1, 1, x, z, flash, 0, -1, lift});
                    }
                }
            }
        }

        // Build preview + hover cursor (same colors as the 3D preview).
        auto* pds = EntityHelper::get_singleton_cmp<PathDrawState>();
        if (pds && pds->hover_valid) {
            constexpr Color PREVIEW_VALID = {100, 220, 130, 100};
            constexpr Color PREVIEW_EXISTING = {180, 180, 180, 80};
            constexpr Color HOVER_NORMAL = {255, 255, 255, 120};
            constexpr Color HOVER_DEMOLISH = {255, 60, 60, 140};
            if (pds->is_drawing) {
                int min_x, min_z, max_x, max_z;
                pds->get_rect(min_x, min_z, max_x, max_z);
                for (int z = min_z; z <= max_z; z++)
                    for (int x = min_x; x <= max_x; x++) {
                        if (!grid->in_bounds(x, z)) continue;
                        const Tile& pt = grid->at(x, z);
                        bool already_path = is_path_tile(pt.type);
                        items.push_back(
                            {static_cast<float>(x + z), 1, 1, x, z,
                             already_path ? PREVIEW_EXISTING : PREVIEW_VALID, 0,
                             -1, pt.height * th * 0.35f});
                    }
            }
            if (grid->in_bounds(pds->hover_x, pds->hover_z)) {
                float hover_lift =
                    grid->at(pds->hover_x, pds->hover_z).height * th * 0.35f;
                items.push_back(
                    {static_cast<float>(pds->hover_x + pds->hover_z), 1, 1,
                     pds->hover_x, pds->hover_z,
                     pds->demolish_mode ? HOVER_DEMOLISH : HOVER_NORMAL, 0, -1,
                     hover_lift});
            }
        }

        // Agents: billboarded peeps (body rect + head + desire pip),
        // snapped into the same draw list so overlap with tiles and
        // buildings sorts by (x+z) like everything else.
        {
            auto agents =
                EntityQuery().whereHasComponent<Agent, Transform>().gen();
            for (Entity& e : agents) {
                if (!e.is_missing<BeingServiced>()) continue;
                auto& agent = e.get<Agent>();
                auto& tf = e.get<Transform>();
                float wx = tf.position.x;
                float wz = tf.position.y;
                int eid = static_cast<int>(e.id);
                wx += hash_scatter(eid * 7 + 3) * SCATTER_RANGE;
                wz += hash_scatter(eid * 13 + 7) * SCATTER_RANGE;
                if (vr) {
                    auto [gx, gz] = grid->world_to_grid(wx, wz);
                    if (gx < vr->min_x || gx > vr->max_x || gz < vr->min_z ||
                        gz > vr->max_z)
                        continue;
                }
                Color body =
                    AGENT_SHIRT_PALETTE[hash_agent_color(eid, agent.color_idx) %
                                        8];
                // VIP guests wear gold, off-palette, so they read at a
                // glance even in a crowd.
                bool is_vip = !e.is_missing<VIPGuest>();
                if (is_vip) body = Color{255, 215, 0, 255};
                float bob = 0.f;
                if (!e.is_missing<WatchingStage>()) {
                    auto& ws = e.get<WatchingStage>();
                    bob = std::sin(ws.watch_timer * 6.0f) * 0.03f;
                }
                if (!e.is_missing<AgentHealth>()) {
                    float hp = e.get<AgentHealth>().hp;
                    if (hp < 0.5f) {
                        float ht = hp / 0.5f;
                        body.r = static_cast<unsigned char>(body.r * ht +
                                                            255 * (1.f - ht));
                        body.g = static_cast<unsigned char>(body.g * ht);
                        body.b = static_cast<unsigned char>(body.b * ht);
                    }
                }
                const char* glyph = nullptr;
                if (!e.is_missing<AgentNeeds>())
                    glyph = iso_urgent_glyph(&e.get<AgentNeeds>());
                int desire_idx = static_cast<int>(agent.want);
                int ref = static_cast<int>(peeps.size());
                float peep_lift = 0.f;
                {
                    auto [pgx, pgz] = grid->world_to_grid(wx, wz);
                    if (grid->in_bounds(pgx, pgz))
                        peep_lift = grid->at(pgx, pgz).height * th * 0.35f;
                }
                peeps.push_back({wx, wz, bob, body, DESIRE_COLORS[desire_idx],
                                 glyph, false, is_vip, peep_lift});
                items.push_back({wx + wz, 3, 3, 0, 0, {}, 0, ref});
                // VIPs always get their star bubble; other agents only
                // get a bubble for an urgent need.
                if (glyph || is_vip)
                    items.push_back({wx + wz, 4, 4, 0, 0, {}, 0, ref});
            }
        }

        // Staff peeps (same billboard shape, role colors).
        {
            auto staff =
                EntityQuery().whereHasComponent<StaffMember, Transform>().gen();
            for (Entity& e : staff) {
                auto& s = e.get<StaffMember>();
                auto& tf = e.get<Transform>();
                Color body =
                    s.role == StaffRole::Guard   ? Color{50, 90, 255, 255}
                    : s.role == StaffRole::Medic ? Color{255, 255, 255, 255}
                                                 : Color{255, 200, 0, 255};
                Color pip = s.role == StaffRole::Medic ? Color{255, 45, 45, 255}
                                                       : Color{25, 25, 25, 255};
                int ref = static_cast<int>(peeps.size());
                float staff_lift = 0.f;
                {
                    auto [sgx, sgz] =
                        grid->world_to_grid(tf.position.x, tf.position.y);
                    if (grid->in_bounds(sgx, sgz))
                        staff_lift = grid->at(sgx, sgz).height * th * 0.35f;
                }
                peeps.push_back({tf.position.x, tf.position.y, 0, body, pip,
                                 nullptr, true, false, staff_lift});
                items.push_back(
                    {tf.position.x + tf.position.y, 3, 3, 0, 0, {}, 0, ref});
            }
        }

        // Death markers (X) and particles (rising rects).
        {
            auto markers = EntityQuery().whereHasComponent<DeathMarker>().gen();
            for (Entity& e : markers) {
                auto& dm = e.get<DeathMarker>();
                float alpha_f = 1.0f;
                if (dm.lifetime < 3.0f) alpha_f = dm.lifetime / 3.0f;
                int ref = static_cast<int>(points.size());
                points.push_back(
                    {dm.position.x,
                     dm.position.y,
                     0.08f,
                     0.15f,
                     {255, 60, 60, static_cast<unsigned char>(alpha_f * 255.f)},
                     1});
                items.push_back(
                    {dm.position.x + dm.position.y, 5, 5, 0, 0, {}, 0, ref});
            }
            auto particles =
                EntityQuery().whereHasComponent<Particle, Transform>().gen();
            for (Entity& e : particles) {
                auto& p = e.get<Particle>();
                auto& tf = e.get<Transform>();
                float life_t = 1.0f - (p.lifetime / p.max_lifetime);
                int ref = static_cast<int>(points.size());
                points.push_back({tf.position.x, tf.position.y,
                                  0.1f + life_t * 0.5f, p.size * 0.02f, p.color,
                                  0});
                items.push_back(
                    {tf.position.x + tf.position.y, 5, 5, 0, 0, {}, 0, ref});
            }
        }

        std::stable_sort(items.begin(), items.end(),
                         [](const IsoDraw& a, const IsoDraw& b) {
                             if (a.depth != b.depth) return a.depth < b.depth;
                             return a.order < b.order;
                         });

        for (const auto& it : items) {
            switch (it.kind) {
                case 0: {
                    vec2 s = cam.iso_project(static_cast<float>(it.x),
                                             static_cast<float>(it.z));
                    if (it.lift > 0.f)
                        iso_draw_raised_tile(s.x, s.y, tw, th, it.color,
                                             it.lift);
                    else
                        iso_draw_tile(s.x, s.y, tw, th, it.color);
                    break;
                }
                case 1: {
                    vec2 s = cam.iso_project(static_cast<float>(it.x),
                                             static_cast<float>(it.z));
                    iso_draw_diamond(s.x, s.y - it.lift, tw, th, it.color);
                    break;
                }
                case 2: {
                    vec2 s = cam.iso_project(static_cast<float>(it.x),
                                             static_cast<float>(it.z));
                    iso_draw_block(s.x, s.y - it.lift, tw, th, it.h, it.color);
                    break;
                }
                case 6: {
                    // Tree blob: brown trunk + two green canopy blobs.
                    vec2 s = cam.iso_project(static_cast<float>(it.x),
                                             static_cast<float>(it.z));
                    s.y -= it.lift;
                    draw_rect(s.x - std::max(tw * 0.03f, 1.0f),
                              s.y - th * 0.12f, std::max(tw * 0.06f, 2.0f),
                              th * 0.14f, Color{120, 80, 40, 255});
                    draw_ellipse(static_cast<int>(s.x),
                                 static_cast<int>(s.y - th * 0.22f), tw * 0.30f,
                                 th * 0.30f, darken_color(it.color, 0.25f));
                    draw_ellipse(static_cast<int>(s.x - tw * 0.04f),
                                 static_cast<int>(s.y - th * 0.30f), tw * 0.22f,
                                 th * 0.22f, lighten_color(it.color, 0.15f));
                    break;
                }
                case 7: {
                    // Lamp: dark post + warm bulb, standing on the
                    // (possibly raised) tile.
                    vec2 s = cam.iso_project(static_cast<float>(it.x),
                                             static_cast<float>(it.z));
                    s.y -= it.lift;
                    float pole_h = std::max(th * 0.35f, 4.0f);
                    draw_rect(s.x - std::max(tw * 0.02f, 1.0f), s.y - pole_h,
                              std::max(tw * 0.04f, 2.0f), pole_h,
                              Color{60, 60, 70, 255});
                    draw_circle(static_cast<int>(s.x),
                                static_cast<int>(s.y - pole_h - 1.0f),
                                std::max(tw * 0.06f, 2.0f),
                                Color{255, 240, 150, 255});
                    break;
                }
                case 3: {
                    const IsoPeep& p = peeps[it.ref];
                    vec2 s = cam.iso_project(p.wx, p.wz);
                    draw_iso_peep(s.x, s.y - p.bob * th - p.lift, tw, th, p);
                    break;
                }
                case 4: {
                    const IsoPeep& p = peeps[it.ref];
                    if (!p.glyph && !p.is_vip) break;
                    vec2 s = cam.iso_project(p.wx, p.wz);
                    // The star bubble wins over the need glyph: a VIP's
                    // identity is the signal; their need still shows
                    // in the gold body + desire pip.
                    if (p.is_vip)
                        draw_iso_star_bubble(s.x, s.y - p.bob * th - p.lift, tw,
                                             th);
                    else
                        draw_iso_bubble(s.x, s.y - p.bob * th - p.lift, tw, th,
                                        p.glyph);
                    break;
                }
                case 5: {
                    const IsoPoint& pt = points[it.ref];
                    vec2 s = cam.iso_project(pt.wx, pt.wz);
                    s.y -= pt.lift * th;
                    if (pt.shape == 1) {
                        float d = pt.size * th * 0.5f;
                        draw_line(static_cast<int>(s.x - d),
                                  static_cast<int>(s.y - d),
                                  static_cast<int>(s.x + d),
                                  static_cast<int>(s.y + d), pt.color);
                        draw_line(static_cast<int>(s.x - d),
                                  static_cast<int>(s.y + d),
                                  static_cast<int>(s.x + d),
                                  static_cast<int>(s.y - d), pt.color);
                    } else {
                        float sz = std::max(pt.size * th, 2.0f);
                        draw_rect(s.x - sz * 0.5f, s.y - sz * 0.5f, sz, sz,
                                  pt.color);
                    }
                    break;
                }
                default:
                    break;
            }
        }

        // Night atmosphere (post-pass, over tiles/peeps: a light wash).
        // Stage light cones: wide warm diamonds over the stage + a
        // fainter wash over the stage floor. Lamp glow pools: warm
        // diamonds around every facility / gate tile. Low alpha over
        // the dark night palette reads as additive-ish light.
        if (night_t > 0.01f) {
            for (int z = z0; z <= z1; z++) {
                for (int x = x0; x <= x1; x++) {
                    const Tile& tile = grid->at(x, z);
                    vec2 s = cam.iso_project(static_cast<float>(x),
                                             static_cast<float>(z));
                    if (tile.type == TileType::Stage) {
                        iso_draw_diamond(
                            s.x, s.y, tw * 1.6f, th * 1.6f,
                            {255, 220, 130,
                             static_cast<unsigned char>(
                                 night_t * NIGHT_GLOW_STAGE_ALPHA)});
                        iso_draw_diamond(
                            s.x, s.y, tw, th,
                            {255, 240, 180,
                             static_cast<unsigned char>(
                                 night_t * NIGHT_GLOW_STAGE_INNER_ALPHA)});
                    } else if (tile.type == TileType::StageFloor) {
                        iso_draw_diamond(
                            s.x, s.y, tw, th,
                            {255, 210, 120,
                             static_cast<unsigned char>(
                                 night_t * NIGHT_GLOW_STAGE_FLOOR_ALPHA)});
                    } else if (is_lamp_tile(tile.type)) {
                        iso_draw_diamond(
                            s.x, s.y, tw * 1.4f, th * 1.4f,
                            {255, 190, 90,
                             static_cast<unsigned char>(
                                 night_t * NIGHT_GLOW_LAMP_ALPHA)});
                    }
                }
            }
        }

        // Rain streaks: short diagonal lines falling across the world
        // viewport (same bounds BeginRenderSystem culls to).
        if (weather.rain) {
            for (int i = 0; i < RAIN_STREAK_COUNT; i++) {
                vec2 p = rain_streak_screen_pos(i, t, 0.f, 1130.f, 44.f, 644.f);
                draw_line(static_cast<int>(p.x), static_cast<int>(p.y),
                          static_cast<int>(p.x - 4), static_cast<int>(p.y + 14),
                          Color{170, 200, 255, 110});
            }
        }
    }

    static void draw_iso_peep(float sx, float sy, float tw, float th,
                              const IsoPeep& p) {
        // Soft ground shadow, then billboarded body + head + desire pip.
        float body_w = std::max(4.0f, tw * 0.20f);
        float body_h = std::max(5.0f, th * 0.62f);
        float head_r = std::max(2.0f, tw * 0.07f);
        draw_ellipse(static_cast<int>(sx), static_cast<int>(sy), body_w * 0.6f,
                     body_w * 0.25f, Color{0, 0, 0, 60});
        draw_rect(sx - body_w * 0.5f, sy - body_h, body_w, body_h, p.body);
        float hx = sx;
        float hy = sy - body_h - head_r + 1.0f;
        draw_circle(static_cast<int>(hx), static_cast<int>(hy), head_r,
                    Color{255, 210, 170, 255});
        float pip_w = std::max(3.0f, tw * 0.08f);
        draw_rect(sx - pip_w * 0.5f, hy - head_r - 3.0f, pip_w, 3.0f, p.pip);
    }

    static void draw_iso_bubble(float sx, float sy, float tw, float th,
                                const char* glyph) {
        if (tw < 24.0f) return;  // too small to read when zoomed out
        float body_h = std::max(5.0f, th * 0.62f);
        float head_r = std::max(2.0f, tw * 0.07f);
        float font_size = 11.0f;
        vec2 m = measure_text_ex(get_font(), glyph, font_size, FONT_SPACING);
        float bw = m.x + 8.0f;
        float bh = m.y + 6.0f;
        float bx = sx - bw * 0.5f;
        float by = sy - body_h - head_r * 2.0f - bh - 6.0f;
        draw_ellipse(static_cast<int>(sx), static_cast<int>(by + bh * 0.5f),
                     bw * 0.5f, bh * 0.5f, Color{255, 255, 255, 255});
        vec2 tail_top{sx - 2.0f, by + bh - 2.0f};
        vec2 tail_tip{sx, by + bh + 4.0f};
        vec2 tail_right{sx + 4.0f, by + bh - 2.0f};
        vec2 tail_mid{sx + 1.0f, by + bh - 1.0f};
        iso_draw_poly4(tail_top, tail_tip, tail_right, tail_mid,
                       Color{255, 255, 255, 255});
        draw_text_ex(get_font(), glyph, {bx + 4.0f, by + 3.0f}, font_size,
                     FONT_SPACING, Color{0, 0, 0, 255});
        auto& vtr = afterhours::testing::VisibleTextRegistry::instance();
        vtr.register_text_if_visible(
            glyph, bx, by, bw, bh, DEFAULT_SCREEN_WIDTH, DEFAULT_SCREEN_HEIGHT);
    }

    // VIP star bubble: same white bubble + tail as the text bubble,
    // but the content is a gold 5-point star drawn as a triangle fan.
    // It cannot be a text glyph: neither game font (Exo2-Bold,
    // Fredoka) contains U+2605 ★, so a "★" string would render as
    // tofu. The star is the VIP marker; the need glyph is suppressed
    // for VIPs (see the kind-4 draw branch).
    static void draw_iso_star_bubble(float sx, float sy, float tw, float th) {
        if (tw < 24.0f) return;  // too small to read when zoomed out
        float body_h = std::max(5.0f, th * 0.62f);
        float head_r = std::max(2.0f, tw * 0.07f);
        float bw = 26.0f;
        float bh = 20.0f;
        float by = sy - body_h - head_r * 2.0f - bh - 6.0f;
        draw_ellipse(static_cast<int>(sx), static_cast<int>(by + bh * 0.5f),
                     bw * 0.5f, bh * 0.5f, Color{255, 255, 255, 255});
        vec2 tail_top{sx - 2.0f, by + bh - 2.0f};
        vec2 tail_tip{sx, by + bh + 4.0f};
        vec2 tail_right{sx + 4.0f, by + bh - 2.0f};
        vec2 tail_mid{sx + 1.0f, by + bh - 1.0f};
        iso_draw_poly4(tail_top, tail_tip, tail_right, tail_mid,
                       Color{255, 255, 255, 255});
        float cx = sx;
        float cy = by + bh * 0.5f;
        float outer = bh * 0.38f;
        float inner = outer * 0.45f;
        vec2 pts[10];
        for (int i = 0; i < 10; i++) {
            float r = (i % 2 == 0) ? outer : inner;
            float a = -static_cast<float>(M_PI) / 2.0f +
                      static_cast<float>(i) * static_cast<float>(M_PI) / 5.0f;
            pts[i] = {cx + std::cos(a) * r, cy + std::sin(a) * r};
        }
        for (int i = 0; i < 10; i++)
            draw_triangle({cx, cy}, pts[i], pts[(i + 1) % 10],
                          Color{255, 215, 0, 255});
    }
};

// Buildings get an extruded two-tone block; flat tiles (grass, path,
// fence, gate, stage floor) stay flat planes.
static bool is_building_tile(TileType type) {
    switch (type) {
        case TileType::Stage:
        case TileType::Bathroom:
        case TileType::Food:
        case TileType::MedTent:
        case TileType::Bar:
        case TileType::SecurityPost:
        case TileType::WaterStation:
        case TileType::Bench:  // low block; Tree draws its own blob
            return true;
        default:
            return false;
    }
}

// True while the 2D iso pass owns world rendering; the legacy 3D
// systems below early-out on it so the two passes never double-draw.
static bool iso_view_active() {
    auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
    return cam && cam->cam.iso_mode;
}

struct RenderGridSystem : System<> {
    void once(float) const override {
        if (iso_view_active()) return;
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;

        auto* vr = EntityHelper::get_singleton_cmp<VisibleRegion>();
        int x0 = vr ? vr->min_x : 0;
        int x1 = vr ? vr->max_x : MAP_SIZE - 1;
        int z0 = vr ? vr->min_z : 0;
        int z1 = vr ? vr->max_z : MAP_SIZE - 1;

        float tile_size = TILESIZE * 0.98f;
        float night_t = get_day_night_t();
        auto weather = atmosphere_weather();
        float shimmer =
            weather.heat ? HEAT_SHIMMER_AMPLITUDE * std::sin(get_time() * 3.0f)
                         : 0.f;

        for (int z = z0; z <= z1; z++) {
            for (int x = x0; x <= x1; x++) {
                const Tile& tile = grid->at(x, z);
                // Raised-plot ground offset (iso uses 0.35 tile-heights
                // of screen lift; 3D uses world units).
                float hy = tile.height * 0.25f;
                Color color =
                    ::tile_color_weather(tile.type, night_t, weather, shimmer);
                draw_plane({x * TILESIZE, 0.01f + hy, z * TILESIZE},
                           {tile_size, tile_size}, color);
                if (tile.type == TileType::Tree) {
                    // Blob-tree: trunk cylinder + canopy sphere.
                    draw_cylinder({x * TILESIZE, hy, z * TILESIZE}, 0.06f,
                                  0.06f, 0.25f, 6, {120, 80, 40, 255});
                    draw_sphere({x * TILESIZE, 0.38f + hy, z * TILESIZE}, 0.32f,
                                color);
                } else if (tile.type == TileType::Lamp) {
                    // Lamp post + bulb.
                    draw_cylinder({x * TILESIZE, hy, z * TILESIZE}, 0.03f,
                                  0.03f, 0.35f, 6, {60, 60, 70, 255});
                    draw_sphere({x * TILESIZE, 0.38f + hy, z * TILESIZE}, 0.07f,
                                {255, 240, 150, 255});
                } else if (is_building_tile(tile.type)) {
                    // Two-tone chunky block: darkened facade sides, lighter
                    // roof face on top. Low enough that agents walking
                    // through a facility stay readable above it.
                    float h = tile.type == TileType::Stage   ? 0.20f
                              : tile.type == TileType::Bench ? 0.06f
                                                             : 0.12f;
                    float w = tile_size * 0.92f;
                    draw_cube({x * TILESIZE, hy + h * 0.5f, z * TILESIZE}, w, h,
                              w, darken_color(color, 0.25f));
                    draw_plane({x * TILESIZE, hy + h + 0.005f, z * TILESIZE},
                               {w, w}, lighten_color(color, 0.40f));
                }
            }
        }

        // Night atmosphere (3D pass): warm glow planes over the stage,
        // stage floor and lamp tiles.
        if (night_t > 0.01f) {
            float ts = TILESIZE * 0.98f;
            for (int z = z0; z <= z1; z++) {
                for (int x = x0; x <= x1; x++) {
                    const Tile& gt = grid->at(x, z);
                    TileType type = gt.type;
                    float gy = 0.06f + gt.height * 0.25f;
                    if (type == TileType::Stage) {
                        draw_plane({x * TILESIZE, gy, z * TILESIZE}, {ts, ts},
                                   {255, 220, 130,
                                    static_cast<unsigned char>(
                                        night_t * NIGHT_GLOW_STAGE_ALPHA)});
                    } else if (type == TileType::StageFloor) {
                        draw_plane(
                            {x * TILESIZE, gy, z * TILESIZE}, {ts, ts},
                            {255, 210, 120,
                             static_cast<unsigned char>(
                                 night_t * NIGHT_GLOW_STAGE_FLOOR_ALPHA)});
                    } else if (is_lamp_tile(type)) {
                        draw_plane({x * TILESIZE, gy, z * TILESIZE}, {ts, ts},
                                   {255, 190, 90,
                                    static_cast<unsigned char>(
                                        night_t * NIGHT_GLOW_LAMP_ALPHA)});
                    }
                }
            }
        }

        // Rain streaks (3D pass): thin falling cylinders across the
        // visible region.
        if (weather.rain) {
            float t = get_time();
            for (int i = 0; i < RAIN_STREAK_COUNT; i++) {
                float wx = (x0 + atmosphere_hash(i * 2 + 1) * (x1 - x0 + 1)) *
                           TILESIZE;
                float wz = (z0 + atmosphere_hash(i * 2 + 2) * (z1 - z0 + 1)) *
                           TILESIZE;
                float wy = std::fmod(
                    atmosphere_hash(i * 3 + 5) * 2.5f + t * 1.5f, 2.5f);
                draw_cylinder({wx, wy, wz}, 0.012f, 0.012f, 0.35f, 4,
                              Color{170, 200, 255, 110});
            }
        }
    }
};

struct RenderStageGlowSystem : System<> {
    void once(float) const override {
        if (iso_view_active()) return;
        auto* sched = EntityHelper::get_singleton_cmp<ArtistSchedule>();
        if (!sched) return;

        float stage_cx = (STAGE_X + STAGE_SIZE / 2.0f) * TILESIZE;
        float stage_cz = (STAGE_Z + STAGE_SIZE / 2.0f) * TILESIZE;

        // Night light cone over the stage (3D pass): a translucent
        // warm cone, on top of the glow planes RenderGridSystem drew.
        float night_t = get_day_night_t();
        if (night_t > 0.01f) {
            auto cone_alpha =
                static_cast<unsigned char>(night_t * NIGHT_GLOW_STAGE_ALPHA);
            draw_cylinder({stage_cx, 0.0f, stage_cz}, 0.15f, 0.9f, 2.0f, 8,
                          Color{255, 220, 130, cone_alpha});
        }

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

// (DESIRE_COLORS / hash_scatter / AGENT_SHIRT_PALETTE / hash_agent_color /
// SCATTER_RANGE live above the iso renderer: the 2D iso pass and the
// legacy 3D pass share them.)

// Staff: distinct cubes per role so roaming staff read at a glance
// (guard = blue, medic = white with red pip, janitor = yellow).
struct RenderStaffSystem : System<> {
    void once(float) const override {
        if (iso_view_active()) return;
        auto* vr = EntityHelper::get_singleton_cmp<VisibleRegion>();
        if (vr && vr->lod != LODLevel::Close) return;
        auto staff =
            EntityQuery().whereHasComponent<StaffMember, Transform>().gen();
        for (Entity& e : staff) {
            auto& s = e.get<StaffMember>();
            auto& tf = e.get<Transform>();
            Color body = s.role == StaffRole::Guard ? Color{50, 90, 255, 255}
                         : s.role == StaffRole::Medic
                             ? Color{255, 255, 255, 255}
                             : Color{255, 200, 0, 255};
            draw_cube({tf.position.x, 0.16f, tf.position.y}, 0.16f, 0.34f,
                      0.16f, body);
            Color pip = s.role == StaffRole::Medic ? Color{255, 45, 45, 255}
                                                   : Color{25, 25, 25, 255};
            draw_cube({tf.position.x, 0.38f, tf.position.y}, 0.10f, 0.08f,
                      0.10f, pip);
        }
    }
};

struct RenderAgentsSystem : System<> {
    void once(float) const override {
        if (iso_view_active()) return;
        auto* vr = EntityHelper::get_singleton_cmp<VisibleRegion>();
        if (vr && vr->lod != LODLevel::Close) return;

        auto* grid = EntityHelper::get_singleton_cmp<Grid>();

        auto agents = EntityQuery().whereHasComponent<Agent, Transform>().gen();

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

            Color body_col =
                AGENT_SHIRT_PALETTE[hash_agent_color(eid, agent.color_idx) % 8];
            // VIP guests wear gold in the top-down pass too (no
            // thought bubbles there, so the gold body + gold pip are
            // the whole signal).
            bool is_vip_3d = !e.is_missing<VIPGuest>();
            if (is_vip_3d) body_col = Color{255, 215, 0, 255};

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
            // Dark outline: a slightly larger dark cube behind the shirt
            // cube reads as a 1px dark outline from the top-down camera.
            draw_cube({wx, base_y, wz}, BODY_W + 0.05f, BODY_H, BODY_W + 0.05f,
                      Color{25, 25, 25, 255});
            draw_cube({wx, base_y + 0.012f, wz}, BODY_W, BODY_H + 0.024f,
                      BODY_W, body_col);

            int desire_idx = static_cast<int>(agent.want);
            Color pip_col = DESIRE_COLORS[desire_idx];
            if (is_vip_3d) pip_col = Color{255, 215, 0, 255};
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
        if (iso_view_active()) return;
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
        if (iso_view_active()) return;
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
        if (iso_view_active()) return;
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
                    const Tile& pt = grid->at(x, z);
                    bool already_path = is_path_tile(pt.type);
                    Color color =
                        already_path ? PREVIEW_EXISTING : PREVIEW_VALID;
                    draw_plane({x * TILESIZE, preview_y + pt.height * 0.25f,
                                z * TILESIZE},
                               {tile_size, tile_size}, color);
                }
            }
        }

        Color cursor_color = pds->demolish_mode ? HOVER_DEMOLISH : HOVER_NORMAL;
        float hover_y = preview_y;
        if (grid->in_bounds(pds->hover_x, pds->hover_z))
            hover_y += grid->at(pds->hover_x, pds->hover_z).height * 0.25f;
        draw_plane({pds->hover_x * TILESIZE, hover_y, pds->hover_z * TILESIZE},
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
        if (iso_view_active()) return;
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
        if (iso_view_active()) return;
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
        if (iso_view_active()) return;
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
        // No matching begin_3d ran in iso mode: end_3d() would unbalance
        // the 3D stack, so skip it there.
        if (cam && !cam->cam.iso_mode) end_3d();
    }
};

void register_render_world_systems(SystemManager& sm) {
    sm.register_render_system(std::make_unique<BeginRenderSystem>());
    sm.register_render_system(std::make_unique<RenderIsoSystem>());
    sm.register_render_system(std::make_unique<RenderGridSystem>());
    sm.register_render_system(std::make_unique<RenderStageGlowSystem>());
    sm.register_render_system(std::make_unique<RenderAgentsSystem>());
    sm.register_render_system(std::make_unique<RenderStaffSystem>());
    sm.register_render_system(std::make_unique<RenderMediumLODSystem>());
    sm.register_render_system(std::make_unique<RenderFarLODSystem>());
    sm.register_render_system(std::make_unique<RenderDensitySystem>());
    sm.register_render_system(std::make_unique<RenderDeathMarkersSystem>());
    sm.register_render_system(std::make_unique<RenderParticlesSystem>());
    sm.register_render_system(std::make_unique<RenderBuildPreviewSystem>());
    sm.register_render_system(std::make_unique<EndMode3DSystem>());
}
