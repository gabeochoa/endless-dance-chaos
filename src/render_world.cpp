// Render domain: 3D world rendering (grid, stage glow, agents, overlays).
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "afterhours/src/core/entity_query.h"
#include "afterhours/src/plugins/e2e_testing/visible_text.h"
#include "components.h"
#include "gfx3d.h"
#include "render_helpers.h"
#include "sprites.h"
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
// (x+z, continuous for agents) + the item's top height + a layer
// order, then items draw back-to-front so nearer objects correctly
// overlap farther ones. The height tie-break matters for tall
// billboards: a peep's head/bubble and a building's roof/canopy share
// a ground depth but sit at different heights, and the taller item
// wins the tie. Buildings are split by part, not by footprint: the
// roof/canopy extras sort at the footprint's BACK edge (so a peep
// standing in front of the building — and its thought bubble, which
// rises into the canopy's screen area — draws after the whole
// building), while the front extras (sign boards, awnings, counters,
// speaker towers) sort at the footprint's FRONT edge (so they are
// never half-covered by the building's own front tiles, and a peep
// behind the building loses to them). A single footprint depth for
// the whole silhouette cannot satisfy both sides: the old
// anchor+2*span-1 depth drew the canopy over front peeps' bubbles,
// eating them.
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
// a slightly inset tile-color diamond reads as a thin grid line without
// a per-tile line call (cheaper, and batch-friendly). The seam is thin
// (1px inset) and dark (50% darken): the old 2px / 35% seam read as a
// bright mint grid over the vivid grass. Seams are for paving only
// (paths / plaza / gate): grass is seamless (see iso_draw_tile_plain)
// so the lawn stops reading as a stitched grid.
static void iso_draw_tile(float cx, float cy, float tw, float th, Color color) {
    iso_draw_diamond(cx, cy, tw, th, darken_color(color, 0.50f));
    iso_draw_diamond(cx, cy, std::max(tw - 1.0f, 1.0f),
                     std::max(th - 0.5f, 1.0f), color);
}

// Seamless ground tile: one full diamond, no inset seam line.
static void iso_draw_tile_plain(float cx, float cy, float tw, float th,
                                Color color) {
    iso_draw_diamond(cx, cy, tw, th, color);
}

// Deterministic per-tile shade variation: hashes (x, z) to a
// brightness factor in [0.94, 1.06] (±6%), so neighbouring grass
// tiles differ just enough to read as texture without banding or
// a visible grid. Same (x, z) always yields the same shade, so the
// variation is stable frame to frame.
static float ground_shade(int x, int z) {
    uint32_t h = static_cast<uint32_t>(x) * 73856093u ^
                 static_cast<uint32_t>(z) * 19349663u;
    h ^= h >> 16;
    h *= 0x45d9f3b;
    h ^= h >> 16;
    return 0.94f + (static_cast<float>(h & 0xFFFF) / 65535.0f) * 0.12f;
}

static Color shade_color(Color c, float f) {
    return Color{static_cast<unsigned char>(std::min(255.0f, c.r * f)),
                 static_cast<unsigned char>(std::min(255.0f, c.g * f)),
                 static_cast<unsigned char>(std::min(255.0f, c.b * f)), c.a};
}

// Raised terrain tile: side skirts from the base diamond up to the
// lifted diamond, so a +1 plot reads as a step, not a floating tile.
// Left skirt darker, right skirt lighter (same shading as iso blocks).
// The lifted top follows the flat-ground seam rule: seamless for
// grass, seamed for paving.
static void iso_draw_raised_tile(float cx, float cy, float tw, float th,
                                 Color color, float h, bool seam) {
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
    if (seam)
        iso_draw_tile(cx, ry, tw, th, color);
    else
        iso_draw_tile_plain(cx, ry, tw, th, color);
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

// The renderer's urgent-glyph lookup is urgent_need_glyph in
// render_helpers.h (shared with the E2E bubble-count assert).

// Hair-cap colors, picked by a hash of the entity id so neighbours
// rarely share a hair color either.
static constexpr Color HAIR_PALETTE[] = {
    {30, 20, 15, 255},  {90, 55, 25, 255}, {200, 160, 60, 255},
    {150, 40, 30, 255}, {60, 60, 60, 255}, {230, 230, 230, 255},
};

struct IsoPeep {
    float wx = 0, wz = 0, bob = 0;
    Color body{255, 255, 255, 255};
    Color hair{30, 20, 15, 255};
    Color pip{255, 255, 255, 255};
    const char* glyph = nullptr;  // thought-bubble glyph, or nullptr
    bool is_staff = false;
    bool is_vip = false;  // gold body + star bubble, not a need glyph
    // The desire pip above the head draws ONLY when show_pip is set:
    // an urgent need, VIP status, or low HP for guests; always for
    // staff (role marker). An always-on pip painted a magenta /
    // purple square on every guest's head — 200 of them in the
    // populated shot, one per desire, which read as noise.
    bool show_pip = false;
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
    // 6 tree, 7 lamp, 8 building roof extras (per-footprint, back
    // edge), 9 building front extras (per-footprint, front edge)
    int kind = 0;
    int x = 0, z = 0;
    Color color{255, 255, 255, 255};
    float h = 0;   // building height px
    int ref = -1;  // IsoPeep / IsoPoint snapshot index
    // Screen-px terrain lift for tiles/buildings/trees/lamps on
    // raised plots (Tile::height * iso_tile_h * 0.35).
    float lift = 0;
    // Building-silhouette extras (kind 8 only): the footprint's tile
    // type and side length in tiles.
    int tile_type = -1;
    int span = 0;
};

// Footprint side length (tiles) for buildings with silhouette extras:
// Stage 4x4, the 2x2 facilities 2x2, the 1x1 posts 1x1. Bench gets a
// plain low block, no extras.
static int silhouette_span(TileType type) {
    switch (type) {
        case TileType::Stage:
            return 4;
        case TileType::Bathroom:
        case TileType::Food:
        case TileType::MedTent:
        case TileType::Bar:
            return 2;
        case TileType::SecurityPost:
        case TileType::WaterStation:
            return 1;
        default:
            return 0;
    }
}

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
        // Viewport cull by projected screen position, BEFORE item
        // construction. BeginRenderSystem's VisibleRegion is the
        // axis-aligned bounding box of the viewport's four corners in
        // grid coords; at default zoom that box iterates 5192 tiles
        // while the cull below keeps ~4191, and every culled tile
        // still paid tile_color_weather + an item + a sort slot +
        // draw calls.
        // Kept items are constructed in the same order as before, so
        // on-screen pixels are unchanged. Bounds are the full
        // 1280x720 texture, not the 1130x644 world viewport, so the
        // cull never depends on UI strips being opaque. Margins cover
        // the largest overhang of any item drawn from one centre:
        // tiles — a 4x4 building footprint reaches +2tw in x and
        // +3.5th below its anchor, roof extras ~2.65th above it;
        // peeps — a thought bubble reaches ~1.6th + 26px above the
        // feet, a shadow ~0.16th below; markers/particles are
        // sub-tile. The peep bounds are asymmetric on purpose: the
        // lower bound is the downward extent, the upper bound the
        // upward extent (a peep below the screen can still show its
        // bubble, never its shadow).
        const float tile_mx = 2.5f * tw;
        const float tile_my = 5.0f * th + 4.0f;
        const float peep_mx = 0.5f * tw + 16.0f;
        const float peep_up = 2.0f * th + 32.0f;
        const float peep_down = 0.5f * th + 4.0f;
        const float fx_mx = tw;
        const float fx_my = th + 4.0f;
        auto tile_on_screen = [&](float sx, float sy) {
            return sx >= -tile_mx &&
                   sx <= static_cast<float>(DEFAULT_SCREEN_WIDTH) + tile_mx &&
                   sy >= -tile_my &&
                   sy <= static_cast<float>(DEFAULT_SCREEN_HEIGHT) + tile_my;
        };
        auto peep_on_screen = [&](float sx, float sy) {
            return sx >= -peep_mx &&
                   sx <= static_cast<float>(DEFAULT_SCREEN_WIDTH) + peep_mx &&
                   sy >= -peep_down &&
                   sy <= static_cast<float>(DEFAULT_SCREEN_HEIGHT) + peep_up;
        };
        auto fx_on_screen = [&](float sx, float sy) {
            return sx >= -fx_mx &&
                   sx <= static_cast<float>(DEFAULT_SCREEN_WIDTH) + fx_mx &&
                   sy >= -fx_my &&
                   sy <= static_cast<float>(DEFAULT_SCREEN_HEIGHT) + fx_my;
        };
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
                // Cull before tile_color_weather / item construction:
                // a rejected tile draws no pixel on screen (the
                // margin covers its footprint + roof extras).
                vec2 ts = cam.iso_project(static_cast<float>(x),
                                          static_cast<float>(z));
                if (!tile_on_screen(ts.x, ts.y)) continue;
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
                    // Base box height: stage tallest, facilities
                    // tall enough that walls + roof + sign read at
                    // the default zoom, bench a low block.
                    float h = tile.type == TileType::Stage   ? th * 0.95f
                              : tile.type == TileType::Bench ? th * 0.25f
                                                             : th * 0.75f;
                    items.push_back(
                        {depth, 2, 2, x, z, weather_color, h, -1, lift});
                    // Per-footprint silhouette extras, emitted from
                    // the anchor tile (no same-type neighbour
                    // up/left), split by part: the roof extras (kind
                    // 8: canopy, back wall, gable, roof blocks, posts)
                    // sort at the footprint's BACK edge, so peeps
                    // standing in front of the building draw after
                    // the whole building, bubbles included; the front
                    // extras (kind 9: sign boards, awnings, counters,
                    // speaker towers) sort at the footprint's FRONT
                    // edge, so the building's own front tiles never
                    // half-cover them. Only the four types with front
                    // extras get a kind-9 item.
                    int span = silhouette_span(tile.type);
                    bool anchor =
                        span > 0 &&
                        !(x > 0 && grid->at(x - 1, z).type == tile.type) &&
                        !(z > 0 && grid->at(x, z - 1).type == tile.type);
                    if (anchor) {
                        items.push_back({depth, 2, 8, x, z, weather_color, h,
                                         -1, lift, static_cast<int>(tile.type),
                                         span});
                        if (tile.type == TileType::Stage ||
                            tile.type == TileType::Food ||
                            tile.type == TileType::MedTent ||
                            tile.type == TileType::Bar) {
                            items.push_back({depth + 2.f * span - 2.f, 2, 9, x,
                                             z, weather_color, h, -1, lift,
                                             static_cast<int>(tile.type),
                                             span});
                        }
                    }
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
                        vec2 ps = cam.iso_project(static_cast<float>(x),
                                                  static_cast<float>(z));
                        if (!tile_on_screen(ps.x, ps.y)) continue;
                        const Tile& pt = grid->at(x, z);
                        bool already_path = is_path_tile(pt.type);
                        items.push_back(
                            {static_cast<float>(x + z), 1, 1, x, z,
                             already_path ? PREVIEW_EXISTING : PREVIEW_VALID, 0,
                             -1, pt.height * th * 0.35f});
                    }
            }
            if (grid->in_bounds(pds->hover_x, pds->hover_z)) {
                vec2 hs = cam.iso_project(static_cast<float>(pds->hover_x),
                                          static_cast<float>(pds->hover_z));
                if (tile_on_screen(hs.x, hs.y)) {
                    float hover_lift =
                        grid->at(pds->hover_x, pds->hover_z).height * th *
                        0.35f;
                    items.push_back(
                        {static_cast<float>(pds->hover_x + pds->hover_z), 1, 1,
                         pds->hover_x, pds->hover_z,
                         pds->demolish_mode ? HOVER_DEMOLISH : HOVER_NORMAL, 0,
                         -1, hover_lift});
                }
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
                // Cull before palette / health / glyph work: the
                // old VisibleRegion grid check is subsumed by this
                // exact screen test (same projection the draw uses).
                {
                    vec2 as = cam.iso_project(wx, wz);
                    if (!peep_on_screen(as.x, as.y)) continue;
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
                bool low_hp = false;
                if (!e.is_missing<AgentHealth>()) {
                    float hp = e.get<AgentHealth>().hp;
                    if (hp < 0.5f) {
                        low_hp = true;
                        float ht = hp / 0.5f;
                        body.r = static_cast<unsigned char>(body.r * ht +
                                                            255 * (1.f - ht));
                        body.g = static_cast<unsigned char>(body.g * ht);
                        body.b = static_cast<unsigned char>(body.b * ht);
                    }
                }
                const char* glyph = nullptr;
                if (!e.is_missing<AgentNeeds>())
                    glyph = urgent_need_glyph(&e.get<AgentNeeds>());
                // Head pip: urgent need / VIP / low HP only. The pip
                // colors the urgent need (or gold for a VIP, red for
                // low HP), never the plain destination desire.
                Color pip_color{255, 255, 255, 255};
                bool show_pip = false;
                if (glyph) {
                    pip_color = urgent_need_pip_color(glyph);
                    show_pip = true;
                } else if (is_vip) {
                    pip_color = Color{255, 215, 0, 255};
                    show_pip = true;
                } else if (low_hp) {
                    pip_color = Color{255, 45, 45, 255};
                    show_pip = true;
                }
                int ref = static_cast<int>(peeps.size());
                float peep_lift = 0.f;
                {
                    auto [pgx, pgz] = grid->world_to_grid(wx, wz);
                    if (grid->in_bounds(pgx, pgz))
                        peep_lift = grid->at(pgx, pgz).height * th * 0.35f;
                }
                Color hair =
                    HAIR_PALETTE[hash_agent_color(eid, agent.color_idx + 11) %
                                 6];
                peeps.push_back({wx, wz, bob, body, hair, pip_color, glyph,
                                 false, is_vip, show_pip, peep_lift});
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
                // Staff had no cull at all: same screen test as guests.
                {
                    vec2 ss = cam.iso_project(tf.position.x, tf.position.y);
                    if (!peep_on_screen(ss.x, ss.y)) continue;
                }
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
                Color hair =
                    HAIR_PALETTE[hash_agent_color(static_cast<int>(e.id), 7) %
                                 6];
                peeps.push_back({tf.position.x, tf.position.y, 0, body, hair,
                                 pip, nullptr, true, false, true, staff_lift});
                items.push_back(
                    {tf.position.x + tf.position.y, 3, 3, 0, 0, {}, 0, ref});
            }
        }

        // Death markers (X) and particles (rising rects).
        {
            auto markers = EntityQuery().whereHasComponent<DeathMarker>().gen();
            for (Entity& e : markers) {
                auto& dm = e.get<DeathMarker>();
                {
                    vec2 ds = cam.iso_project(dm.position.x, dm.position.y);
                    if (!fx_on_screen(ds.x, ds.y)) continue;
                }
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
                {
                    vec2 ps = cam.iso_project(tf.position.x, tf.position.y);
                    if (!fx_on_screen(ps.x, ps.y)) continue;
                }
                float life_t = 1.0f - (p.lifetime / p.max_lifetime);
                int ref = static_cast<int>(points.size());
                points.push_back({tf.position.x, tf.position.y,
                                  0.1f + life_t * 0.5f, p.size * 0.02f, p.color,
                                  0});
                items.push_back(
                    {tf.position.x + tf.position.y, 5, 5, 0, 0, {}, 0, ref});
            }
        }

        // Painter's sort: ground depth (x+z), then the item's top
        // height, then the layer order. The height tie-break keeps
        // tall items in the right order on one depth diagonal: a
        // peep (body + head ≈ 1.3 tiles) wins over a building block
        // (≤ 0.95 tiles) at the same depth, a thought bubble (peep
        // height + bubble) wins over both, and a roof extra (block
        // + extras ≈ +1.2 tiles) wins over a peep standing exactly
        // on the anchor diagonal — the building occludes the peep
        // behind it. Front extras sit half a tile above the block
        // they face (they hang on the face's outside, and a peep
        // in front still wins the tie by a hair). Fx (death
        // markers / particles) sort last at any depth.
        auto sort_height = [&](const IsoDraw& it) {
            switch (it.kind) {
                case 3:
                    return th * 1.30f;  // peep: body + head
                case 4:
                    return th * 1.30f + 28.f;  // bubble: peep + bubble
                case 5:
                    return 1e9f;  // fx: always last at a depth
                case 6:
                    return th * 0.60f;  // tree canopy top
                case 7:
                    return th * 0.40f;  // lamp bulb
                case 8:
                    return it.h + th * 1.20f;  // roof extras' top
                case 9:
                    return it.h + th * 0.50f;  // front extras
                default:
                    return it.h;  // tile / overlay 0, building block h
            }
        };
        std::stable_sort(items.begin(), items.end(),
                         [&](const IsoDraw& a, const IsoDraw& b) {
                             if (a.depth != b.depth) return a.depth < b.depth;
                             float ha = sort_height(a), hb = sort_height(b);
                             if (ha != hb) return ha < hb;
                             return a.order < b.order;
                         });

        for (const auto& it : items) {
            switch (it.kind) {
                case 0: {
                    vec2 s = cam.iso_project(static_cast<float>(it.x),
                                             static_cast<float>(it.z));
                    TileType gtype = TileType::Grass;
                    if (grid && grid->in_bounds(it.x, it.z))
                        gtype = grid->at(it.x, it.z).type;
                    // Ground seam rule: grass / locked lawn is
                    // seamless with per-tile shade variation; paving
                    // (paths, gate) keeps the seam; the dance floor
                    // is a checker two-tone with a dark border edge
                    // where it meets anything but floor / stage.
                    if (gtype == TileType::StageFloor) {
                        float ck = ((it.x + it.z) & 1) ? 1.07f : 0.93f;
                        Color floor_c = shade_color(it.color, ck);
                        bool border = false;
                        if (grid) {
                            static constexpr int NB[4][2] = {
                                {1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                            for (auto& d : NB) {
                                int nx = it.x + d[0], nz = it.z + d[1];
                                if (!grid->in_bounds(nx, nz)) {
                                    border = true;
                                    break;
                                }
                                TileType nt = grid->at(nx, nz).type;
                                if (nt != TileType::StageFloor &&
                                    nt != TileType::Stage) {
                                    border = true;
                                    break;
                                }
                            }
                        }
                        if (it.lift > 0.f)
                            iso_draw_raised_tile(s.x, s.y, tw, th, floor_c,
                                                 it.lift, true);
                        else if (border)
                            iso_draw_tile(s.x, s.y, tw, th, floor_c);
                        else
                            iso_draw_tile_plain(s.x, s.y, tw, th, floor_c);
                    } else if (gtype == TileType::Grass ||
                               gtype == TileType::Locked) {
                        Color shaded =
                            shade_color(it.color, ground_shade(it.x, it.z));
                        if (it.lift > 0.f)
                            iso_draw_raised_tile(s.x, s.y, tw, th, shaded,
                                                 it.lift, false);
                        else
                            iso_draw_tile_plain(s.x, s.y, tw, th, shaded);
                    } else {
                        if (it.lift > 0.f)
                            iso_draw_raised_tile(s.x, s.y, tw, th, it.color,
                                                 it.lift, true);
                        else
                            iso_draw_tile(s.x, s.y, tw, th, it.color);
                    }
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
                case 8: {
                    // Per-footprint roof extras: geometric shapes
                    // only, no text — the building reads by silhouette.
                    draw_iso_silhouette(cam, static_cast<float>(it.x),
                                        static_cast<float>(it.z), it.span,
                                        static_cast<TileType>(it.tile_type),
                                        it.lift, tw, th, it.h, it.color, 0);
                    break;
                }
                case 9: {
                    // Per-footprint front extras (sign boards, awnings,
                    // counters, speaker towers).
                    draw_iso_silhouette(cam, static_cast<float>(it.x),
                                        static_cast<float>(it.z), it.span,
                                        static_cast<TileType>(it.tile_type),
                                        it.lift, tw, th, it.h, it.color, 1);
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
                    // Skip non-glow tiles before projecting, and
                    // cull glow tiles that draw no pixel on screen.
                    if (tile.type != TileType::Stage &&
                        tile.type != TileType::StageFloor &&
                        !is_lamp_tile(tile.type))
                        continue;
                    vec2 s = cam.iso_project(static_cast<float>(x),
                                             static_cast<float>(z));
                    if (!tile_on_screen(s.x, s.y)) continue;
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

    // Peep proportions, as fractions of the tile size: ~1.4x the old
    // body (tw*0.20 / th*0.62) so peeps read as individuals at the
    // default zoom. The bubble drawers share these constants so the
    // bubble stays attached to the head.
    static constexpr float PEEP_BODY_W_FRAC = 0.28f;
    static constexpr float PEEP_BODY_H_FRAC = 0.87f;
    static constexpr float PEEP_HEAD_R_FRAC = 0.10f;
    static constexpr Color PEEP_OUTLINE = {20, 20, 20, 255};
    static constexpr Color PEEP_SKIN = {255, 210, 170, 255};

    static void draw_iso_peep(float sx, float sy, float tw, float th,
                              const IsoPeep& p) {
        // Drop-shadow ellipse on the ground, then an outlined
        // two-tone body: shirt-color torso, darkened legs, skin-tone
        // head with a varied hair cap, desire pip on top. The dark
        // outline separates each peep from the ground and from its
        // neighbours, so a crowd reads as individuals, not a blob.
        float body_w = std::max(5.0f, tw * PEEP_BODY_W_FRAC);
        float body_h = std::max(7.0f, th * PEEP_BODY_H_FRAC);
        float head_r = std::max(2.5f, tw * PEEP_HEAD_R_FRAC);
        draw_ellipse(static_cast<int>(sx), static_cast<int>(sy), body_w * 0.70f,
                     body_w * 0.28f, Color{0, 0, 0, 90});
        float torso_h = body_h * 0.62f;
        draw_rect(sx - body_w * 0.5f - 1.0f, sy - body_h - 1.0f, body_w + 2.0f,
                  body_h + 2.0f, PEEP_OUTLINE);
        draw_rect(sx - body_w * 0.5f, sy - body_h, body_w, torso_h, p.body);
        draw_rect(sx - body_w * 0.5f, sy - body_h + torso_h, body_w,
                  body_h - torso_h, darken_color(p.body, 0.35f));
        float hx = sx;
        float hy = sy - body_h - head_r + 1.0f;
        draw_circle(static_cast<int>(hx), static_cast<int>(hy), head_r + 1.0f,
                    PEEP_OUTLINE);
        draw_circle(static_cast<int>(hx), static_cast<int>(hy - head_r * 0.35f),
                    head_r * 0.95f, p.hair);
        draw_circle(static_cast<int>(hx), static_cast<int>(hy + head_r * 0.35f),
                    head_r * 0.92f, PEEP_SKIN);
        // Desire pip: urgent need / VIP / low HP (guests) or role
        // marker (staff) only — never a plain destination color on
        // every head.
        if (p.show_pip) {
            float pip_w = std::max(3.0f, tw * 0.10f);
            draw_rect(sx - pip_w * 0.5f, hy - head_r * 1.4f - 4.0f, pip_w, 4.0f,
                      p.pip);
        }
    }

    static void draw_iso_bubble(float sx, float sy, float tw, float th,
                                const char* glyph) {
        // Gated low: default-zoom tiles are ~21px, and an urgent bubble
        // must still show at the default framing. Below ~12px tiles the
        // bubble would be bigger than the peep is tall.
        if (tw < 12.0f) return;
        float body_h = std::max(7.0f, th * PEEP_BODY_H_FRAC);
        float head_r = std::max(2.5f, tw * PEEP_HEAD_R_FRAC);
        // Sprite first: the Kenney emote face replaces the text glyph
        // inside the same white bubble shell + tail. When sprites are
        // off or the texture failed to load, the old measured text
        // bubble is the fallback — same geometry, same registry call.
        SpriteId face = sprite_for_need_glyph(glyph);
        bool use_sprite =
            face != SpriteId::Count && sprites_enabled() && sprite_loaded(face);
        float bw, bh;
        float bx, by;
        vec2 text_m{0.f, 0.f};
        if (use_sprite) {
            bw = 26.0f;
            bh = 20.0f;
        } else {
            float font_size = 11.0f;
            text_m =
                measure_text_ex(get_font(), glyph, font_size, FONT_SPACING);
            bw = text_m.x + 8.0f;
            bh = text_m.y + 6.0f;
        }
        bx = sx - bw * 0.5f;
        by = sy - body_h - head_r * 2.0f - bh - 6.0f;
        draw_ellipse(static_cast<int>(sx), static_cast<int>(by + bh * 0.5f),
                     bw * 0.5f, bh * 0.5f, Color{255, 255, 255, 255});
        vec2 tail_top{sx - 2.0f, by + bh - 2.0f};
        vec2 tail_tip{sx, by + bh + 4.0f};
        vec2 tail_right{sx + 4.0f, by + bh - 2.0f};
        vec2 tail_mid{sx + 1.0f, by + bh - 1.0f};
        iso_draw_poly4(tail_top, tail_tip, tail_right, tail_mid,
                       Color{255, 255, 255, 255});
        if (use_sprite) {
            draw_sprite_centered(face, sx, by + bh * 0.5f,
                                 std::min(bw, bh) * 0.8f);
        } else {
            draw_text_ex(get_font(), glyph, {bx + 4.0f, by + 3.0f}, 11.0f,
                         FONT_SPACING, Color{0, 0, 0, 255});
        }
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
        // Same gate as the need bubble: visible at default zoom.
        if (tw < 12.0f) return;
        float body_h = std::max(7.0f, th * PEEP_BODY_H_FRAC);
        float head_r = std::max(2.5f, tw * PEEP_HEAD_R_FRAC);
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

    // Per-footprint silhouette extras, drawn from the anchor tile
    // (ax, az) of a span x span footprint. All geometry is derived
    // from the footprint diamond's extreme vertices — the top vertex
    // of the anchor tile, the right vertex of the (ax+span-1, az)
    // tile, the bottom vertex of the far tile, the left vertex of
    // the (ax, az+span-1) tile — so the extras sit on the same
    // diamond the per-tile blocks draw. Half-tile corner points
    // would skew the shape into a parallelogram. iso_project
    // applies rotation, so "front" follows the view. All shapes
    // are geometric — no text labels.
    // `parts`: 0 = roof/upper parts (canopy, back wall, gable, roof
    // blocks, posts/slabs — sorted at the footprint's back edge),
    // 1 = front parts (sign boards, awnings, counters, speaker
    // towers — sorted at the footprint's front edge). The caller
    // emits one draw item per part so each sorts at its own edge.
    static void draw_iso_silhouette(const IsometricCamera& cam, float ax,
                                    float az, int span, TileType type,
                                    float lift, float tw, float th,
                                    float block_h, Color color, int parts) {
        if (span <= 0) return;
        // Diamond vertices at ground level: TL = north, TR = east,
        // BR = south (front), BL = west.
        vec2 tl = cam.iso_project(ax, az);
        tl.y -= th * 0.5f;
        vec2 tr = cam.iso_project(ax + span - 1, az);
        tr.x += tw * 0.5f;
        vec2 br = cam.iso_project(ax + span - 1, az + span - 1);
        br.y += th * 0.5f;
        vec2 bl = cam.iso_project(ax, az + span - 1);
        bl.x -= tw * 0.5f;
        tl.y -= lift;
        tr.y -= lift;
        br.y -= lift;
        bl.y -= lift;
        // Footprint centre at ground, then the roof level above it.
        vec2 c =
            cam.iso_project(ax + span * 0.5f - 0.5f, az + span * 0.5f - 0.5f);
        c.y -= lift;
        float roof_y = c.y - block_h;
        auto at_roof = [&](vec2 p) {
            p.y -= block_h;
            return p;
        };
        // Front sign board: a panel in the building's own color,
        // standing on the roof toward the front vertex, carrying a
        // white geometric glyph (no text). Boards sit above the
        // roof so they never cover the walls.
        auto sign_board = [&](float bx, float bottom_y, float bw, float bh,
                              Color bg, int glyph,
                              SpriteId glyph_sprite = SpriteId::Count) {
            float bx0 = bx - bw * 0.5f;
            float by0 = bottom_y - bh;
            draw_rect(bx0, by0, bw, bh, bg);
            draw_rect_lines(bx0, by0, bw, bh, Color{0, 0, 0, 160});
            // Sprite glyph first (MedTent cross, Water drop); the
            // geometric glyph below is the fallback when sprites are
            // off or the texture failed to load.
            if (glyph_sprite != SpriteId::Count &&
                draw_sprite_centered(glyph_sprite, bx, by0 + bh * 0.5f,
                                     std::min(bw, bh) * 0.8f))
                return;
            Color g{255, 255, 255, 255};
            float gcx = bx, gcy = by0 + bh * 0.5f;
            switch (glyph) {
                case 0: {  // cross (medtent)
                    float vw = std::max(2.0f, bw * 0.16f);
                    float vh = bh * 0.72f;
                    float hw = bw * 0.52f;
                    float hh = std::max(2.0f, bh * 0.22f);
                    draw_rect(gcx - vw * 0.5f, gcy - vh * 0.5f, vw, vh, g);
                    draw_rect(gcx - hw * 0.5f, gcy - hh * 0.5f, hw, hh, g);
                    break;
                }
                case 1: {  // person pictogram (WC)
                    float hr = std::max(1.5f, bh * 0.16f);
                    draw_circle(static_cast<int>(gcx),
                                static_cast<int>(gcy - bh * 0.22f), hr, g);
                    float pw = std::max(2.0f, bw * 0.16f);
                    draw_rect(gcx - pw * 0.5f, gcy - bh * 0.02f, pw, bh * 0.42f,
                              g);
                    break;
                }
                case 2: {  // plate (food)
                    draw_ellipse(static_cast<int>(gcx), static_cast<int>(gcy),
                                 bw * 0.30f, bh * 0.30f, g);
                    draw_ellipse(static_cast<int>(gcx), static_cast<int>(gcy),
                                 bw * 0.16f, bh * 0.16f, bg);
                    draw_circle(static_cast<int>(gcx), static_cast<int>(gcy),
                                std::max(1.0f, bh * 0.08f), g);
                    break;
                }
                case 3: {  // droplet (water)
                    float dr = std::max(1.5f, bh * 0.22f);
                    draw_circle(static_cast<int>(gcx),
                                static_cast<int>(gcy + bh * 0.10f), dr, g);
                    draw_triangle({gcx - dr, gcy + bh * 0.10f},
                                  {gcx + dr, gcy + bh * 0.10f},
                                  {gcx, gcy - bh * 0.36f}, g);
                    break;
                }
                case 4: {  // shield (security)
                    float sw = bw * 0.20f;
                    draw_rect(gcx - sw, gcy - bh * 0.32f, sw * 2.0f, bh * 0.38f,
                              g);
                    draw_triangle({gcx - sw, gcy + bh * 0.06f},
                                  {gcx + sw, gcy + bh * 0.06f},
                                  {gcx, gcy + bh * 0.36f}, g);
                    break;
                }
                default:
                    break;
            }
        };
        // Board geometry for this footprint: centred between the
        // roof centre and the front vertex, standing on the roof.
        auto board_front = [&](int glyph,
                               SpriteId glyph_sprite = SpriteId::Count) {
            vec2 br_roof = at_roof(br);
            float bx = (c.x + br_roof.x) * 0.5f;
            float bottom_y = (roof_y + br_roof.y) * 0.5f;
            float bw = tw * (span == 1 ? 0.55f : 0.85f);
            float bh = th * (span == 1 ? 0.60f : 0.75f);
            sign_board(bx, bottom_y, bw, bh, color, glyph, glyph_sprite);
        };

        switch (type) {
            case TileType::Stage: {
                // Raised platform (the per-tile blocks) + back wall
                // + roof canopy on 4 posts (roof parts) + speaker
                // towers (front parts): the back wall is a vertical
                // face along the rear roof edge, rising to the
                // canopy; the posts join the canopy corners to the
                // roof.
                if (parts == 0) {
                    float canopy_h = block_h + th * 0.55f;
                    auto up = [&](vec2 p) {
                        p.y -= canopy_h;
                        return p;
                    };
                    iso_draw_poly4(at_roof(tl), at_roof(tr), up(tr), up(tl),
                                   darken_color(color, 0.55f));
                    float post_w = std::max(2.0f, tw * 0.06f);
                    vec2 corners[4] = {tl, tr, br, bl};
                    for (vec2 corner : corners) {
                        draw_rect(corner.x - post_w * 0.5f, corner.y - canopy_h,
                                  post_w, canopy_h - block_h,
                                  darken_color(color, 0.60f));
                    }
                    iso_draw_poly4(up(tl), up(tr), up(br), up(bl),
                                   darken_color(color, 0.45f));
                    vec2 mid{(up(tl).x + up(br).x) * 0.5f,
                             (up(tl).y + up(br).y) * 0.5f};
                    auto inset = [&](vec2 p) {
                        return vec2{mid.x + (p.x - mid.x) * 0.55f,
                                    mid.y + (p.y - mid.y) * 0.55f};
                    };
                    iso_draw_poly4(inset(up(tl)), inset(up(tr)), inset(up(br)),
                                   inset(up(bl)), lighten_color(color, 0.25f));
                    // Music-note sprite on the canopy panel (purely
                    // additive: no geometric glyph is replaced here, so
                    // there is no fallback to draw when sprites are off).
                    {
                        vec2 q[4] = {inset(up(tl)), inset(up(tr)),
                                     inset(up(br)), inset(up(bl))};
                        float qx0 = std::min(std::min(q[0].x, q[1].x),
                                             std::min(q[2].x, q[3].x));
                        float qx1 = std::max(std::max(q[0].x, q[1].x),
                                             std::max(q[2].x, q[3].x));
                        float qy0 = std::min(std::min(q[0].y, q[1].y),
                                             std::min(q[2].y, q[3].y));
                        float qy1 = std::max(std::max(q[0].y, q[1].y),
                                             std::max(q[2].y, q[3].y));
                        draw_sprite_centered(
                            SpriteId::Music, mid.x, mid.y,
                            std::min(qx1 - qx0, qy1 - qy0) * 0.6f);
                    }
                }
                if (parts == 1) {
                    // Speaker towers: tall dark columns with two light
                    // cones, standing on the ground at the front-edge
                    // midpoints.
                    vec2 fronts[2] = {
                        {(bl.x + br.x) * 0.5f, (bl.y + br.y) * 0.5f},
                        {(br.x + tr.x) * 0.5f, (br.y + tr.y) * 0.5f}};
                    for (vec2 f : fronts) {
                        float w = std::max(2.0f, tw * 0.22f);
                        float hs = th * 1.15f;
                        draw_rect(f.x - w * 0.5f, f.y - hs, w, hs,
                                  Color{25, 25, 25, 255});
                        draw_rect(f.x - w * 0.25f, f.y - hs + hs * 0.12f,
                                  w * 0.5f, hs * 0.18f,
                                  Color{200, 200, 200, 255});
                        draw_rect(f.x - w * 0.25f, f.y - hs + hs * 0.52f,
                                  w * 0.5f, hs * 0.18f,
                                  Color{200, 200, 200, 255});
                    }
                }
                break;
            }
            case TileType::Bathroom: {
                // WC = small block + sign: a small extruded block
                // on the roof, with the front sign board (building
                // color, white person pictogram) standing on it.
                // All roof parts: the board sits on the centre roof
                // block, above the front tiles' reach.
                if (parts == 0) {
                    float bh2 = th * 0.40f;
                    iso_draw_block(c.x, roof_y, tw * 0.70f, th * 0.70f, bh2,
                                   color);
                    float bw = tw * 0.70f;
                    float bh = th * 0.70f;
                    sign_board(c.x, roof_y - bh2, bw, bh, color, 1);
                }
                break;
            }
            case TileType::Food: {
                // Food = flat awning + sign: a striped slab jutting
                // straight out from the two front roof edges (white
                // / stall color, alternating), plus the front sign
                // board (building color, white plate glyph).
                // All front parts: the awning hangs off the front
                // faces, so it sorts at the front edge.
                if (parts == 1) {
                    float drop = th * 0.60f;
                    vec2 rtr = at_roof(tr);
                    vec2 rbr = at_roof(br), rbl = at_roof(bl);
                    auto panel = [&](vec2 p1, vec2 p2, Color pc) {
                        iso_draw_poly4(p1, p2, {p2.x, p2.y + drop},
                                       {p1.x, p1.y + drop}, pc);
                    };
                    auto striped_edge = [&](vec2 p1, vec2 p2) {
                        vec2 m1{(2.0f * p1.x + p2.x) / 3.0f,
                                (2.0f * p1.y + p2.y) / 3.0f};
                        vec2 m2{(p1.x + 2.0f * p2.x) / 3.0f,
                                (p1.y + 2.0f * p2.y) / 3.0f};
                        panel(p1, m1, Color{255, 255, 255, 255});
                        panel(m1, m2, color);
                        panel(m2, p2, Color{255, 255, 255, 255});
                    };
                    striped_edge(rbl, rbr);
                    striped_edge(rbr, rtr);
                    board_front(2);
                }
                break;
            }
            case TileType::MedTent: {
                // Med = tent peak roof + sign: a peaked (gable)
                // roof — white front gable, shaded side gable,
                // apex above the roof — plus the front sign board
                // (building color, white cross glyph).
                if (parts == 0) {
                    vec2 rtr = at_roof(tr);
                    vec2 rbr = at_roof(br), rbl = at_roof(bl);
                    vec2 apex{c.x, roof_y - th * 0.55f};
                    draw_triangle(rbr, rtr, apex, lighten_color(color, 0.35f));
                    draw_triangle(rbl, rbr, apex, Color{255, 255, 255, 255});
                }
                if (parts == 1) board_front(0, SpriteId::Cross);
                break;
            }
            case TileType::Bar: {
                // Bar = long counter + roof: a flat roof slab on
                // two posts above the roof (roof parts), and a long
                // counter across the front in the building's color
                // with two bottles on top (front part).
                if (parts == 0) {
                    vec2 rtl = at_roof(tl), rtr = at_roof(tr);
                    vec2 rbr = at_roof(br), rbl = at_roof(bl);
                    auto inset_slab = [&](vec2 p) {
                        return vec2{
                            c.x + (p.x - c.x) * 0.90f,
                            (roof_y - th * 0.60f) + (p.y - roof_y) * 0.90f};
                    };
                    iso_draw_poly4(inset_slab(rtl), inset_slab(rtr),
                                   inset_slab(rbr), inset_slab(rbl),
                                   Color{110, 70, 35, 255});
                    float post_w = std::max(2.0f, tw * 0.05f);
                    vec2 post_xs[2] = {
                        {(rbl.x + rbr.x) * 0.5f, (rbl.y + rbr.y) * 0.5f},
                        {(rbr.x + rtr.x) * 0.5f, (rbr.y + rtr.y) * 0.5f}};
                    for (vec2 f : post_xs) {
                        draw_rect(f.x - post_w * 0.5f, roof_y - th * 0.60f,
                                  post_w, th * 0.60f, Color{110, 70, 35, 255});
                    }
                }
                if (parts == 1) {
                    vec2 f{(bl.x + 2 * br.x + tr.x) * 0.25f,
                           (bl.y + 2 * br.y + tr.y) * 0.25f};
                    float cw = tw * 1.5f;
                    float ch = std::max(3.0f, th * 0.50f);
                    float cy = f.y - th * 0.1f - ch;
                    draw_rect(f.x - cw * 0.5f, cy, cw, ch, color);
                    draw_rect(f.x - cw * 0.5f, cy, cw,
                              std::max(1.0f, ch * 0.25f),
                              lighten_color(color, 0.30f));
                    float bw = std::max(2.0f, tw * 0.08f);
                    float bh = std::max(3.0f, th * 0.30f);
                    draw_rect(f.x - cw * 0.30f - bw * 0.5f, cy - bh, bw, bh,
                              Color{0, 180, 80, 255});
                    draw_rect(f.x + cw * 0.30f - bw * 0.5f, cy - bh, bw, bh,
                              Color{255, 60, 60, 255});
                }
                break;
            }
            case TileType::SecurityPost: {
                // Security = post tower: a tall post rising from
                // the roof with a cap diamond, and the front sign
                // board (building color, white shield glyph)
                // mounted on the post. All roof parts (1x1: no front
                // tiles to clip them).
                if (parts == 0) {
                    float post_w = std::max(2.5f, tw * 0.10f);
                    float post_h = th * 1.15f;
                    draw_rect(c.x - post_w * 0.5f, roof_y - post_h, post_w,
                              post_h, darken_color(color, 0.25f));
                    iso_draw_diamond(c.x, roof_y - post_h, tw * 0.75f,
                                     th * 0.75f, lighten_color(color, 0.20f));
                    float bw = tw * 0.55f;
                    float bh = th * 0.60f;
                    sign_board(c.x, roof_y - th * 0.35f, bw, bh, color, 4);
                }
                break;
            }
            case TileType::WaterStation: {
                // Water = small block + sign: a small extruded
                // block on the roof, with the front sign board
                // (building color, white droplet glyph) on it.
                // All roof parts (1x1: no front tiles to clip them).
                if (parts == 0) {
                    float bh2 = th * 0.40f;
                    iso_draw_block(c.x, roof_y, tw * 0.70f, th * 0.70f, bh2,
                                   color);
                    float bw = tw * 0.70f;
                    float bh = th * 0.70f;
                    sign_board(c.x, roof_y - bh2, bw, bh, color, 3,
                               SpriteId::Drop);
                }
                break;
            }
            default:
                break;
        }
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

            // Head pip: same gate as the iso pass — urgent need / VIP /
            // low HP only, colored by the urgent need, never a plain
            // destination color on every head.
            const char* glyph_3d = nullptr;
            if (!e.is_missing<AgentNeeds>())
                glyph_3d = urgent_need_glyph(&e.get<AgentNeeds>());
            bool low_hp_3d =
                !e.is_missing<AgentHealth>() && e.get<AgentHealth>().hp < 0.5f;
            if (glyph_3d || is_vip_3d || low_hp_3d) {
                Color pip_col = glyph_3d    ? urgent_need_pip_color(glyph_3d)
                                : is_vip_3d ? Color{255, 215, 0, 255}
                                            : Color{255, 45, 45, 255};
                float pip_y = base_y + BODY_H * 0.5f + PIP_H * 0.5f;
                draw_cube({wx, pip_y, wz}, PIP_W, PIP_H, PIP_W, pip_col);
            }
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
