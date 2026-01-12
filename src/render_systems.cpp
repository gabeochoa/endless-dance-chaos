// AFTER_HOURS_REPLACE_LOGGING and log.h must come first
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "afterhours/src/core/entity_query.h"
#include "components.h"
#include "game.h"
#include "systems.h"
#include "vec_util.h"

// Render texture from main.cpp for MCP screenshots
extern raylib::RenderTexture2D g_render_texture;

struct BeginRenderSystem : System<> {
    void once(float) const override {
        // Render to texture for MCP screenshot support
        raylib::BeginTextureMode(g_render_texture);
        raylib::ClearBackground(raylib::Color{40, 44, 52, 255});

        auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
        raylib::BeginMode3D(cam->cam.camera);
    }
};

struct RenderGroundSystem : System<> {
    // Draw grass ground plane
    float size = 10.0f * TILESIZE;
    raylib::Color grassColor = {58, 95, 40, 255};  // Deep grass green

    void once(float) const override {
        // Main grass plane
        raylib::DrawPlane({0.0f, 0.0f, 0.0f}, {size * 2.0f, size * 2.0f},
                          grassColor);

        // TODO: Render individual grass blades for visual detail
        // Could use instanced rendering with small quads or billboards
        // varying height, slight color variation, and gentle wind sway
    }
};

struct RenderAgentsSystem : System<Transform, Agent, HasStress> {
    void render_agent_shape(raylib::Vector3 pos, float radius,
                            FacilityType want, raylib::Color color) const {
        switch (want) {
            case FacilityType::Stage:
                raylib::DrawSphere(pos, radius, color);
                break;
            case FacilityType::Food:
                raylib::DrawCylinder(pos, 0.f, radius, radius * 2.f, 4, color);
                break;
            case FacilityType::Bathroom:
                raylib::DrawCube(pos, radius * 1.5f, radius * 1.5f,
                                 radius * 1.5f, color);
                break;
        }
    }

    void render_stress_sparks(raylib::Vector3 pos, float stress) const {
        if (stress <= 0.9f) return;

        for (int i = 0; i < 3; i++) {
            float spark_x = pos.x + ((rand() % 100) / 100.f - 0.5f) * 0.5f;
            float spark_y = pos.y + ((rand() % 100) / 100.f) * 0.5f;
            float spark_z = pos.z + ((rand() % 100) / 100.f - 0.5f) * 0.5f;
            raylib::DrawSphere({spark_x, spark_y, spark_z}, 0.03f,
                               raylib::YELLOW);
        }
    }

    void for_each_with(const Entity& e, const Transform& t, const Agent& a,
                       const HasStress& s, float) const override {
        raylib::Color color =
            raylib::ColorLerp(raylib::GRAY, raylib::RED, s.stress);

        float jitter_x = 0.f, jitter_z = 0.f;
        if (s.stress > 0.7f) {
            float jitter_strength = (s.stress - 0.7f) * 0.3f;
            jitter_x = ((rand() % 100) / 100.f - 0.5f) * jitter_strength;
            jitter_z = ((rand() % 100) / 100.f - 0.5f) * jitter_strength;
        }

        raylib::Vector3 pos = {t.position.x + jitter_x, t.radius,
                               t.position.y + jitter_z};

        // If inside a facility, offset position to slot location
        if (e.has<InsideFacility>()) {
            const InsideFacility& inside = e.get<InsideFacility>();
            pos.x += inside.slot_offset.x;
            pos.z += inside.slot_offset.y;
        }

        render_agent_shape(pos, t.radius, a.want, color);
        render_stress_sparks(pos, s.stress);
    }
};

struct RenderAttractionsSystem : System<Transform, Attraction> {
    void for_each_with(const Entity&, const Transform& t, const Attraction&,
                       float) const override {
        raylib::DrawCube(vec::to3(t.position), 1.0f, 0.5f, 1.0f,
                         raylib::PURPLE);
        raylib::DrawCubeWires(vec::to3(t.position), 1.0f, 0.5f, 1.0f,
                              raylib::DARKPURPLE);
    }
};

struct RenderFacilitiesSystem : System<Transform, Facility> {
    static constexpr float SIZE = 1.5f;
    static constexpr float HEIGHT = 0.5f;

    void render_stage(const Transform& t, raylib::Color color,
                      raylib::Color wire_color) const {
        raylib::Vector3 pos = {t.position.x, HEIGHT * 0.5f, t.position.y};
        // Wireframe only so we can see people inside
        raylib::DrawCubeWires(pos, SIZE, HEIGHT, SIZE, wire_color);
        // Floor
        raylib::DrawPlane({t.position.x, 0.02f, t.position.y}, {SIZE, SIZE},
                          raylib::Fade(color, 0.3f));
    }

    void render_enclosed(const Transform& t, raylib::Color color,
                         raylib::Color wire_color) const {
        float hs = SIZE * 0.5f;
        float hh = HEIGHT * 0.5f;
        float wall = 0.05f;

        // 3 walls (open on entry side - negative X)
        raylib::DrawCube({t.position.x + hs, hh, t.position.y}, wall, HEIGHT,
                         SIZE, color);
        raylib::DrawCube({t.position.x, hh, t.position.y - hs}, SIZE, HEIGHT,
                         wall, color);
        raylib::DrawCube({t.position.x, hh, t.position.y + hs}, SIZE, HEIGHT,
                         wall, color);

        // Floor
        raylib::DrawPlane({t.position.x, 0.02f, t.position.y}, {SIZE, SIZE},
                          raylib::Fade(color, 0.3f));

        // Wireframe outline
        raylib::Vector3 pos = {t.position.x, hh, t.position.y};
        raylib::DrawCubeWires(pos, SIZE, HEIGHT, SIZE, wire_color);
    }

    void for_each_with(const Entity&, const Transform& t, const Facility& f,
                       float) const override {
        switch (f.type) {
            case FacilityType::Stage:
                render_stage(t, raylib::MAGENTA, raylib::DARKPURPLE);
                break;
            case FacilityType::Bathroom:
                render_enclosed(t, raylib::BLUE, raylib::DARKBLUE);
                break;
            case FacilityType::Food:
                render_enclosed(t, raylib::YELLOW, raylib::ORANGE);
                break;
        }
    }
};

struct RenderPathsSystem : System<Transform, PathTile> {
    static constexpr raylib::Color SIDEWALK_MAIN = {180, 175, 165, 255};
    static constexpr raylib::Color SIDEWALK_EDGE = {140, 135, 125, 255};
    static constexpr raylib::Color CONGESTED_COLOR = {200, 80, 80, 255};
    static constexpr raylib::Color OVERCROWDED_COLOR = {220, 50, 50, 255};

    raylib::Color get_path_color(float congestion_ratio) const {
        if (congestion_ratio <= 0.5f) {
            return SIDEWALK_MAIN;  // Normal
        } else if (congestion_ratio <= 1.0f) {
            // Lerp from normal to congested (yellow-ish warning)
            float t = (congestion_ratio - 0.5f) * 2.0f;
            return raylib::ColorLerp(SIDEWALK_MAIN, CONGESTED_COLOR, t);
        } else {
            // Overcrowded - red, with pulse
            float pulse = 0.5f + 0.5f * sinf((float) raylib::GetTime() * 4.0f);
            return raylib::ColorLerp(CONGESTED_COLOR, OVERCROWDED_COLOR, pulse);
        }
    }

    void for_each_with(const Entity&, const Transform& t, const PathTile& tile,
                       float) const override {
        float y = 0.01f;  // Slightly above ground to prevent z-fighting

        // Get color based on congestion
        raylib::Color path_color = get_path_color(tile.congestion_ratio());

        // Draw tile as a filled plane
        raylib::DrawPlane({t.position.x, y, t.position.y},
                          {TILESIZE * 0.95f, TILESIZE * 0.95f}, path_color);

        // Draw edge border
        raylib::Color edge_color =
            tile.congestion_ratio() > 1.0f
                ? raylib::ColorLerp(SIDEWALK_EDGE, raylib::RED, 0.5f)
                : SIDEWALK_EDGE;

        float hs = TILESIZE * 0.475f;  // Half size with slight inset
        raylib::Vector3 corners[4] = {
            {t.position.x - hs, y + 0.005f, t.position.y - hs},
            {t.position.x + hs, y + 0.005f, t.position.y - hs},
            {t.position.x + hs, y + 0.005f, t.position.y + hs},
            {t.position.x - hs, y + 0.005f, t.position.y + hs}};

        for (int i = 0; i < 4; i++) {
            raylib::DrawLine3D(corners[i], corners[(i + 1) % 4], edge_color);
        }
    }
};

// Render ghost tiles for pending path changes (3D, before EndMode3D)
struct RenderPathPreviewSystem : System<BuilderState> {
    void for_each_with(const Entity&, const BuilderState& builder,
                       float) const override {
        if (!builder.active) return;
        if (builder.tool != BuildTool::Path) return;  // Only in path mode

        // Render all pending ghost tiles
        for (const auto& pending : builder.pending_tiles) {
            float x = pending.grid_x * TILESIZE;
            float z = pending.grid_z * TILESIZE;

            raylib::Color color =
                pending.is_removal
                    ? raylib::Color{255, 80, 80, 150}   // Red ghost (removal)
                    : raylib::Color{80, 255, 80, 150};  // Green ghost (add)

            raylib::DrawPlane({x, 0.03f, z},
                              {TILESIZE * 0.95f, TILESIZE * 0.95f}, color);
        }

        // Render cursor hover preview (more transparent)
        if (builder.hover_valid &&
            !builder.is_pending_at(builder.hover_grid_x,
                                   builder.hover_grid_z)) {
            float x = builder.hover_grid_x * TILESIZE;
            float z = builder.hover_grid_z * TILESIZE;

            raylib::Color color =
                builder.path_exists_at_hover
                    ? raylib::Color{255, 100, 100, 80}   // Red hint (remove)
                    : raylib::Color{100, 255, 100, 80};  // Green hint (add)

            raylib::DrawPlane({x, 0.02f, z}, {TILESIZE, TILESIZE}, color);
        }
    }
};

// TODO put all thsee tile helpers together
// Check if a grid position has a path tile (render-side helper)
static bool render_is_on_path(int grid_x, int grid_z) {
    auto path_tiles = EntityQuery().whereHasComponent<PathTile>().gen();

    for (const Entity& tile : path_tiles) {
        const PathTile& pt = tile.get<PathTile>();
        if (pt.grid_x == grid_x && pt.grid_z == grid_z) return true;
    }
    return false;
}

// Render 3D preview for facility placement
struct RenderFacilityPreviewSystem : System<BuilderState, FestivalProgress> {
    static constexpr float SIZE = 1.5f;
    static constexpr float HEIGHT = 0.5f;

    void for_each_with(const Entity&, const BuilderState& builder,
                       const FestivalProgress& progress, float) const override {
        if (!builder.active || !builder.hover_valid) return;
        if (builder.tool == BuildTool::Path) return;  // Path preview elsewhere

        float x = builder.hover_grid_x * TILESIZE;
        float z = builder.hover_grid_z * TILESIZE;

        // Check if placement is valid - must be on a path
        bool on_path =
            render_is_on_path(builder.hover_grid_x, builder.hover_grid_z);

        bool has_slot = false;
        raylib::Color base_color = raylib::WHITE;

        switch (builder.tool) {
            case BuildTool::Bathroom:
                has_slot = progress.can_place(FacilityType::Bathroom);
                base_color = raylib::SKYBLUE;
                break;
            case BuildTool::Food:
                has_slot = progress.can_place(FacilityType::Food);
                base_color = raylib::YELLOW;
                break;
            case BuildTool::Stage:
                has_slot = progress.can_place(FacilityType::Stage);
                base_color = raylib::MAGENTA;
                break;
            case BuildTool::Path:
                break;  // Already handled above
        }

        bool valid = on_path && has_slot;

        // Ghost color: green-tinted if valid, red-tinted if invalid
        raylib::Color ghost_color =
            valid ? raylib::Color{base_color.r, base_color.g, base_color.b, 120}
                  : raylib::Color{255, 80, 80, 120};

        raylib::Vector3 pos = {x, HEIGHT * 0.5f, z};

        // Draw ghost facility
        raylib::DrawCube(pos, SIZE, HEIGHT, SIZE, ghost_color);
        raylib::DrawCubeWires(pos, SIZE, HEIGHT, SIZE,
                              valid ? raylib::WHITE : raylib::RED);

        // Draw X if invalid
        if (!valid) {
            raylib::DrawLine3D(
                {x - SIZE * 0.4f, HEIGHT + 0.1f, z - SIZE * 0.4f},
                {x + SIZE * 0.4f, HEIGHT + 0.1f, z + SIZE * 0.4f}, raylib::RED);
            raylib::DrawLine3D(
                {x + SIZE * 0.4f, HEIGHT + 0.1f, z - SIZE * 0.4f},
                {x - SIZE * 0.4f, HEIGHT + 0.1f, z + SIZE * 0.4f}, raylib::RED);
        }
    }
};

struct EndMode3DSystem : System<> {
    void once(float) const override {
        auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
        if (cam) raylib::EndMode3D();
    }
};

// Renders circular progress indicators above stages (2D overlay)
struct RenderStageIndicatorsSystem : System<Transform, Facility, StageInfo> {
    static constexpr float INDICATOR_RADIUS = 30.0f;

    struct IndicatorStyle {
        raylib::Color bg;
        raylib::Color fg;
        raylib::Color icon;
        const char* icon_char;
    };

    static IndicatorStyle get_style(StageState state) {
        switch (state) {
            case StageState::Idle:
                return {{60, 60, 60, 200},
                        {100, 100, 100, 220},
                        {150, 150, 150, 255},
                        "Z"};
            case StageState::Announcing:
                return {{80, 60, 20, 200},
                        {255, 200, 50, 230},
                        {255, 230, 100, 255},
                        "!"};
            case StageState::Performing:
                return {{30, 80, 30, 200},
                        {80, 220, 80, 230},
                        {150, 255, 150, 255},
                        "*"};
            case StageState::Clearing:
                return {{80, 30, 30, 200},
                        {220, 60, 60, 230},
                        {255, 100, 100, 255},
                        "X"};
        }
        return {
            {60, 60, 60, 200}, {100, 100, 100, 220}, {150, 150, 150, 255}, "?"};
    }

    void for_each_with(const Entity&, const Transform& t, const Facility&,
                       const StageInfo& info, float) const override {
        auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
        if (!cam) return;

        // Project 3D position to screen space
        raylib::Vector3 world_pos = {t.position.x, 1.5f, t.position.y};
        raylib::Vector2 screen_pos =
            raylib::GetWorldToScreen(world_pos, cam->cam.camera);

        // Skip if off screen
        if (screen_pos.x < -50 || screen_pos.x > DEFAULT_SCREEN_WIDTH + 50 ||
            screen_pos.y < -50 || screen_pos.y > DEFAULT_SCREEN_HEIGHT + 50)
            return;

        float radius = INDICATOR_RADIUS;
        float progress = info.get_progress();
        auto [bg_color, fg_color, icon_color, icon_char] =
            get_style(info.state);

        // Background circle
        raylib::DrawCircle((int) screen_pos.x, (int) screen_pos.y, radius,
                           bg_color);

        // Progress arc - starts at top (-90°), sweeps clockwise
        float start_angle = -90.0f;
        float end_angle = start_angle + (360.0f * progress);
        raylib::DrawCircleSector(screen_pos, radius - 3, start_angle, end_angle,
                                 32, fg_color);

        // Progress line showing current position
        if (progress > 0.01f && progress < 0.99f) {
            float angle_rad = end_angle * 0.0174533f;  // DEG2RAD
            float line_x = screen_pos.x + (radius - 3) * cosf(angle_rad);
            float line_y = screen_pos.y + (radius - 3) * sinf(angle_rad);
            raylib::DrawLine((int) screen_pos.x, (int) screen_pos.y,
                             (int) line_x, (int) line_y, raylib::WHITE);
        }

        // Inner circle for icon background
        raylib::DrawCircle((int) screen_pos.x, (int) screen_pos.y,
                           radius * 0.5f, raylib::Color{30, 30, 30, 230});

        // Icon character (centered)
        int font_size = (int) (radius * 0.7f);
        int text_width = raylib::MeasureText(icon_char, font_size);
        raylib::DrawText(icon_char, (int) (screen_pos.x - text_width / 2),
                         (int) (screen_pos.y - font_size / 2), font_size,
                         icon_color);

        // Outer ring
        raylib::DrawCircleLines((int) screen_pos.x, (int) screen_pos.y, radius,
                                raylib::Color{200, 200, 200, 180});
    }
};

// Minimap showing agent distribution and facilities
struct RenderMinimapSystem : System<> {
    static constexpr int MAP_SIZE = 150;
    static constexpr int MAP_MARGIN = 8;
    static constexpr float WORLD_SIZE = 15.0f;  // World units to show

    // Convert world position to minimap position (rotated to match camera)
    static raylib::Vector2 world_to_map(vec2 world_pos, int map_x, int map_y,
                                        float rotation) {
        // Rotate world position to match camera orientation
        float cos_r = cosf(rotation);
        float sin_r = sinf(rotation);
        float rx = world_pos.x * cos_r - world_pos.y * sin_r;
        float ry = world_pos.x * sin_r + world_pos.y * cos_r;

        // Scale to fit minimap (need larger world size due to rotation)
        float scale =
            MAP_SIZE / (WORLD_SIZE * 2.0f * 1.414f);  // sqrt(2) for diagonal
        float center = MAP_SIZE / 2.0f;
        return {map_x + center + rx * scale, map_y + center + ry * scale};
    }

    void once(float) const override {
        // Position: bottom-right corner
        int map_x = DEFAULT_SCREEN_WIDTH - MAP_SIZE - MAP_MARGIN;
        int map_y = DEFAULT_SCREEN_HEIGHT - MAP_SIZE - MAP_MARGIN;

        // Get camera rotation so minimap rotates with camera
        auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
        float rotation = cam ? cam->cam.yaw : M_PI / 4.0f;

        // Background
        raylib::DrawRectangle(map_x - 2, map_y - 2, MAP_SIZE + 4, MAP_SIZE + 4,
                              raylib::Color{20, 20, 20, 220});
        raylib::DrawRectangleLines(map_x - 2, map_y - 2, MAP_SIZE + 4,
                                   MAP_SIZE + 4,
                                   raylib::Color{100, 100, 100, 255});

        // Draw path tiles
        auto paths = EntityQuery()
                         .whereHasComponent<Transform>()
                         .whereHasComponent<PathTile>()
                         .gen();
        for (const Entity& tile : paths) {
            vec2 pos = tile.get<Transform>().position;
            raylib::Vector2 mp = world_to_map(pos, map_x, map_y, rotation);
            raylib::DrawRectangle((int) mp.x - 2, (int) mp.y - 2, 4, 4,
                                  raylib::Color{120, 115, 105, 255});
        }

        // Draw facilities
        auto facilities = EntityQuery()
                              .whereHasComponent<Transform>()
                              .whereHasComponent<Facility>()
                              .gen();
        for (const Entity& fac : facilities) {
            vec2 pos = fac.get<Transform>().position;
            raylib::Vector2 mp = world_to_map(pos, map_x, map_y, rotation);
            FacilityType type = fac.get<Facility>().type;

            raylib::Color color;
            switch (type) {
                case FacilityType::Stage:
                    color = raylib::MAGENTA;
                    break;
                case FacilityType::Bathroom:
                    color = raylib::BLUE;
                    break;
                case FacilityType::Food:
                    color = raylib::YELLOW;
                    break;
            }
            raylib::DrawRectangle((int) mp.x - 4, (int) mp.y - 4, 8, 8, color);
        }

        // Draw attractions
        auto attractions = EntityQuery()
                               .whereHasComponent<Transform>()
                               .whereHasComponent<Attraction>()
                               .gen();
        for (const Entity& attr : attractions) {
            vec2 pos = attr.get<Transform>().position;
            raylib::Vector2 mp = world_to_map(pos, map_x, map_y, rotation);
            raylib::DrawRectangle((int) mp.x - 3, (int) mp.y - 3, 6, 6,
                                  raylib::PURPLE);
        }

        // Draw agents
        auto agents = EntityQuery()
                          .whereHasComponent<Transform>()
                          .whereHasComponent<Agent>()
                          .whereHasComponent<HasStress>()
                          .gen();
        for (const Entity& agent : agents) {
            vec2 pos = agent.get<Transform>().position;
            float stress = agent.get<HasStress>().stress;
            raylib::Vector2 mp = world_to_map(pos, map_x, map_y, rotation);
            raylib::Color color =
                raylib::ColorLerp(raylib::GREEN, raylib::RED, stress);
            raylib::DrawCircle((int) mp.x, (int) mp.y, 2, color);
        }

        // Draw camera view rectangle (like Civilization)
        // Since minimap rotates with camera, the view rectangle is axis-aligned
        if (cam) {
            float view_height = cam->cam.camera.fovy;
            float aspect =
                (float) DEFAULT_SCREEN_WIDTH / (float) DEFAULT_SCREEN_HEIGHT;
            float view_width = view_height * aspect;

            // Hide rectangle when viewing more than 20% of the minimap area
            float map_area = (WORLD_SIZE * 2.0f) * (WORLD_SIZE * 2.0f);
            float view_area = view_width * view_height;
            if (view_area < map_area * 0.20f) {
                // Camera target is the center of the view
                vec2 center = {cam->cam.target.x, cam->cam.target.z};
                float hw = view_width / 2.0f;
                float hh = view_height / 2.0f;

                // Simple axis-aligned corners (minimap rotation handles camera
                // yaw)
                vec2 corners[4];
                corners[0] = {center.x - hw, center.y - hh};
                corners[1] = {center.x + hw, center.y - hh};
                corners[2] = {center.x + hw, center.y + hh};
                corners[3] = {center.x - hw, center.y + hh};

                // Draw the view rectangle on minimap
                for (int i = 0; i < 4; i++) {
                    raylib::Vector2 a =
                        world_to_map(corners[i], map_x, map_y, rotation);
                    raylib::Vector2 b = world_to_map(corners[(i + 1) % 4],
                                                     map_x, map_y, rotation);
                    raylib::DrawLineV(a, b, raylib::WHITE);
                }
            }
        }

        // Label
        raylib::DrawText("MAP", map_x + 4, map_y + 4, 10,
                         raylib::Color{150, 150, 150, 200});
    }
};

// Data layer overlay showing demand heatmap (toggled with TAB)
// Like Cities: Skylines info views for traffic, happiness, etc.
struct RenderDataLayerSystem
    : System<GameState, ProvidesCamera, DemandHeatmap> {
    static raylib::Color get_demand_color(FacilityType type) {
        switch (type) {
            case FacilityType::Bathroom:
                return raylib::Color{80, 140, 255, 255};  // Blue
            case FacilityType::Food:
                return raylib::Color{255, 200, 50, 255};  // Yellow
            case FacilityType::Stage:
                return raylib::Color{255, 80, 200, 255};  // Magenta
        }
        return raylib::WHITE;
    }

    void for_each_with(const Entity&, const GameState& state,
                       const ProvidesCamera& cam, const DemandHeatmap& heatmap,
                       float) const override {
        if (!state.show_data_layer) return;

        // Draw demand heatmap on all grid cells with demand
        for (const auto& [grid_pos, demands] : heatmap.demand_grid) {
            auto [grid_x, grid_z] = grid_pos;
            float world_x = grid_x * TILESIZE;
            float world_z = grid_z * TILESIZE;

            raylib::Vector3 world_pos = {world_x, 0.5f, world_z};
            raylib::Vector2 screen_pos =
                raylib::GetWorldToScreen(world_pos, cam.cam.camera);

            // Skip if off screen
            if (screen_pos.x < 0 || screen_pos.x > DEFAULT_SCREEN_WIDTH ||
                screen_pos.y < 0 || screen_pos.y > DEFAULT_SCREEN_HEIGHT)
                continue;

            int filter = heatmap.display_filter;
            int total = 0;
            raylib::Color display_color = raylib::WHITE;

            if (filter == 0) {
                // Show all - blend colors based on demand mix
                total = demands[0] + demands[1] + demands[2];
                if (total == 0) continue;

                // Find dominant demand type
                int max_idx = 0;
                if (demands[1] > demands[max_idx]) max_idx = 1;
                if (demands[2] > demands[max_idx]) max_idx = 2;
                display_color =
                    get_demand_color(static_cast<FacilityType>(max_idx));
            } else {
                // Show specific type
                int type_idx = filter - 1;
                total = demands[type_idx];
                if (total == 0) continue;
                display_color =
                    get_demand_color(static_cast<FacilityType>(type_idx));
            }

            // Intensity based on count (cap at 10 for full intensity)
            float intensity = std::min(1.0f, total / 10.0f);
            display_color.a = (unsigned char) (100 + 155 * intensity);

            // Draw colored circle at position
            int radius = (int) (8 + 12 * intensity);
            raylib::DrawCircle((int) screen_pos.x, (int) screen_pos.y, radius,
                               raylib::Fade(display_color, 0.6f));

            // Draw count text
            auto text = fmt::format("{}", total);
            int text_w = raylib::MeasureText(text.c_str(), 10);
            raylib::DrawText(text.c_str(), (int) screen_pos.x - text_w / 2,
                             (int) screen_pos.y - 5, 10, raylib::WHITE);
        }

        // Draw filter legend at bottom
        int legend_y = DEFAULT_SCREEN_HEIGHT - 50;
        raylib::DrawText("[TAB] Demand View", 10, legend_y, 14, raylib::YELLOW);

        const char* filter_names[] = {"All", "Bathroom", "Food", "Stage"};
        raylib::Color filter_colors[] = {raylib::WHITE, raylib::SKYBLUE,
                                         raylib::YELLOW, raylib::MAGENTA};

        for (int i = 0; i < 4; i++) {
            int x = 10 + i * 80;
            raylib::Color col = filter_colors[i];
            if (heatmap.display_filter == i) {
                col = raylib::WHITE;
                raylib::DrawRectangle(x - 2, legend_y + 18, 76, 18,
                                      raylib::Color{80, 80, 80, 200});
            }
            auto label = fmt::format(
                "[{}] {}", i == 0 ? "0" : std::to_string(i), filter_names[i]);
            raylib::DrawText(label.c_str(), x, legend_y + 20, 12, col);
        }
    }
};

// Show confirmation UI when pending path tiles exist
struct RenderBuilderUISystem : System<BuilderState> {
    void for_each_with(const Entity&, const BuilderState& builder,
                       float) const override {
        if (!builder.has_pending()) return;

        int adds = 0, removes = 0;
        for (const auto& p : builder.pending_tiles) {
            if (p.is_removal)
                removes++;
            else
                adds++;
        }

        // Bottom-center confirmation UI
        int ui_y = DEFAULT_SCREEN_HEIGHT - 60;
        int ui_x = DEFAULT_SCREEN_WIDTH / 2 - 100;

        // Background
        raylib::DrawRectangle(ui_x, ui_y, 200, 50,
                              raylib::Color{40, 40, 40, 220});
        raylib::DrawRectangleLines(ui_x, ui_y, 200, 50, raylib::WHITE);

        // Text showing pending changes
        auto text = fmt::format("+{} -{}", adds, removes);
        raylib::DrawText(text.c_str(), ui_x + 10, ui_y + 8, 16, raylib::WHITE);

        // Confirm/Cancel hints
        raylib::DrawText("[Enter] Confirm", ui_x + 10, ui_y + 28, 12,
                         raylib::GREEN);
        raylib::DrawText("[Esc] Cancel", ui_x + 110, ui_y + 28, 12,
                         raylib::RED);
    }
};

// Render facility placement mode indicator and preview
struct RenderBuildToolUISystem : System<BuilderState, ProvidesCamera> {
    void for_each_with(const Entity&, const BuilderState& builder,
                       const ProvidesCamera& /*cam*/, float) const override {
        // Show current tool at top center
        const char* tool_name = "";
        raylib::Color tool_color = raylib::WHITE;

        switch (builder.tool) {
            case BuildTool::Path:
                tool_name = "PATH TOOL";
                tool_color = raylib::Color{100, 200, 100, 255};
                break;
            case BuildTool::Bathroom:
                tool_name = "BATHROOM TOOL";
                tool_color = raylib::SKYBLUE;
                break;
            case BuildTool::Food:
                tool_name = "FOOD TOOL";
                tool_color = raylib::YELLOW;
                break;
            case BuildTool::Stage:
                tool_name = "STAGE TOOL";
                tool_color = raylib::MAGENTA;
                break;
        }

        // Draw tool indicator at top center
        int text_w = raylib::MeasureText(tool_name, 20);
        int x = (DEFAULT_SCREEN_WIDTH - text_w) / 2;
        raylib::DrawRectangle(x - 10, 40, text_w + 20, 28,
                              raylib::Color{0, 0, 0, 180});
        raylib::DrawText(tool_name, x, 44, 20, tool_color);
    }
};

// Render build tools panel
struct RenderBuildToolsPanelSystem : System<FestivalProgress, BuilderState> {
    void for_each_with(const Entity&, const FestivalProgress& progress,
                       const BuilderState& builder, float) const override {
        // Draw slots UI in top-right area
        int ui_x = DEFAULT_SCREEN_WIDTH - 220;
        int ui_y = 40;

        // Background
        raylib::DrawRectangle(ui_x - 5, ui_y - 5, 215, 85,
                              raylib::Color{30, 30, 30, 200});
        raylib::DrawRectangleLines(ui_x - 5, ui_y - 5, 215, 105,
                                   raylib::Color{80, 80, 80, 255});

        raylib::DrawText("BUILD MODE", ui_x, ui_y, 14, raylib::LIGHTGRAY);

        // Path mode row
        int path_y = ui_y + 18;
        bool path_selected = builder.tool == BuildTool::Path;
        if (path_selected) {
            raylib::DrawRectangle(ui_x - 3, path_y - 2, 208, 18,
                                  raylib::Color{100, 200, 100, 60});
        }
        raylib::Color path_color = path_selected ? raylib::WHITE : raylib::GRAY;
        raylib::DrawText("[1] Path", ui_x, path_y, 14, path_color);

        struct SlotInfo {
            FacilityType type;
            const char* key;
            const char* name;
            raylib::Color color;
        };

        SlotInfo slots[] = {
            {FacilityType::Bathroom, "2", "Bath", raylib::SKYBLUE},
            {FacilityType::Food, "3", "Food", raylib::YELLOW},
            {FacilityType::Stage, "4", "Stage", raylib::MAGENTA},
        };

        for (int i = 0; i < 3; i++) {
            int row_y = ui_y + 38 + i * 20;
            auto& slot = slots[i];

            int placed = progress.get_placed(slot.type);
            int total = progress.get_slots(slot.type);
            bool can_place = progress.can_place(slot.type);

            // Highlight if this tool is selected
            bool is_selected = (builder.tool == BuildTool::Bathroom &&
                                slot.type == FacilityType::Bathroom) ||
                               (builder.tool == BuildTool::Food &&
                                slot.type == FacilityType::Food) ||
                               (builder.tool == BuildTool::Stage &&
                                slot.type == FacilityType::Stage);

            if (is_selected) {
                raylib::DrawRectangle(ui_x - 3, row_y - 2, 208, 18,
                                      raylib::Color{slot.color.r, slot.color.g,
                                                    slot.color.b, 60});
            }

            // Key hint
            auto key_text = fmt::format("[{}]", slot.key);
            raylib::Color key_color = can_place ? slot.color : raylib::DARKGRAY;
            raylib::DrawText(key_text.c_str(), ui_x, row_y, 14, key_color);

            // Name and count
            auto slot_text = fmt::format("{}: {}/{}", slot.name, placed, total);
            raylib::Color text_color =
                can_place ? raylib::WHITE : raylib::DARKGRAY;
            raylib::DrawText(slot_text.c_str(), ui_x + 35, row_y, 14,
                             text_color);

            // "NEW!" indicator if slot just unlocked
            if (can_place && placed < total) {
                float blink =
                    0.5f + 0.5f * sinf((float) raylib::GetTime() * 4.0f);
                raylib::DrawText("+", ui_x + 140, row_y, 14,
                                 raylib::Fade(raylib::GREEN, blink));
            }
        }
    }
};

struct RenderUISystem : System<> {
    void once(float) const override {
        auto* state = EntityHelper::get_singleton_cmp<GameState>();

        // Title
        raylib::DrawText("Endless Dance Chaos", 10, 10, 20, raylib::WHITE);
        raylib::DrawFPS(DEFAULT_SCREEN_WIDTH - 100, 10);

        if (!state) return;

        // Stress meter
        int meter_x = 10;
        int meter_y = 40;
        int meter_w = 200;
        int meter_h = 20;

        raylib::DrawRectangle(meter_x, meter_y, meter_w, meter_h,
                              raylib::Color{40, 40, 40, 200});

        float stress_ratio = state->global_stress;
        raylib::Color stress_color =
            raylib::ColorLerp(raylib::GREEN, raylib::RED, stress_ratio);
        raylib::DrawRectangle(meter_x, meter_y, (int) (meter_w * stress_ratio),
                              meter_h, stress_color);
        raylib::DrawRectangleLines(meter_x, meter_y, meter_w, meter_h,
                                   raylib::WHITE);

        // Stress label
        auto stress_text = fmt::format("Stress: {:.0f}%", stress_ratio * 100.f);
        raylib::DrawText(stress_text.c_str(), meter_x + 5, meter_y + 3, 14,
                         raylib::WHITE);

        // Time
        int minutes = (int) state->game_time / 60;
        int seconds = (int) state->game_time % 60;
        auto time_text = fmt::format("Time: {}:{:02d}", minutes, seconds);
        raylib::DrawText(time_text.c_str(), meter_x, meter_y + meter_h + 5, 16,
                         raylib::WHITE);

        // Agent count
        int agent_count =
            (int) EntityQuery().whereHasComponent<Agent>().gen_count();
        auto agent_text = fmt::format("Agents: {}", agent_count);
        raylib::DrawText(agent_text.c_str(), meter_x, meter_y + meter_h + 24,
                         16, raylib::WHITE);

        // Game Over screen
        if (state->is_game_over()) {
            // Fade overlay
            float fade = std::min(state->game_over_timer * 0.5f, 0.7f);
            raylib::DrawRectangle(0, 0, DEFAULT_SCREEN_WIDTH,
                                  DEFAULT_SCREEN_HEIGHT,
                                  raylib::Fade(raylib::BLACK, fade));

            // Game Over text with animation
            float scale = 1.0f + 0.1f * sinf(state->game_over_timer * 2.0f);
            int font_size = (int) (60 * scale);
            const char* game_over = "CROWD PANIC!";
            int text_w = raylib::MeasureText(game_over, font_size);
            int text_x = (DEFAULT_SCREEN_WIDTH - text_w) / 2;
            int text_y = DEFAULT_SCREEN_HEIGHT / 2 - 60;

            raylib::DrawText(game_over, text_x + 3, text_y + 3, font_size,
                             raylib::BLACK);  // Shadow
            raylib::DrawText(game_over, text_x, text_y, font_size, raylib::RED);

            // Stats
            auto stats =
                fmt::format("Survived: {}:{:02d}\nFinal Stress: {:.0f}%",
                            minutes, seconds, stress_ratio * 100.f);
            int stats_w = raylib::MeasureText("Final Stress: 100%", 24);
            raylib::DrawText(stats.c_str(),
                             (DEFAULT_SCREEN_WIDTH - stats_w) / 2, text_y + 80,
                             24, raylib::WHITE);

            // Restart prompt
            if (state->game_over_timer > 2.0f) {
                float blink = 0.5f + 0.5f * sinf(state->game_over_timer * 3.0f);
                const char* restart = "Press R to Restart";
                int restart_w = raylib::MeasureText(restart, 20);
                raylib::DrawText(
                    restart, (DEFAULT_SCREEN_WIDTH - restart_w) / 2,
                    text_y + 160, 20, raylib::Fade(raylib::WHITE, blink));
            }
        }
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
    sm.register_render_system(std::make_unique<RenderGroundSystem>());
    sm.register_render_system(std::make_unique<RenderPathsSystem>());
    sm.register_render_system(
        std::make_unique<RenderPathPreviewSystem>());  // Ghost path tiles
    sm.register_render_system(
        std::make_unique<RenderFacilityPreviewSystem>());  // Ghost facilities
    sm.register_render_system(std::make_unique<RenderAttractionsSystem>());
    sm.register_render_system(std::make_unique<RenderFacilitiesSystem>());
    sm.register_render_system(std::make_unique<RenderAgentsSystem>());
    sm.register_render_system(std::make_unique<EndMode3DSystem>());
    sm.register_render_system(std::make_unique<RenderStageIndicatorsSystem>());
    sm.register_render_system(std::make_unique<RenderMinimapSystem>());
    sm.register_render_system(std::make_unique<RenderDataLayerSystem>());
    sm.register_render_system(
        std::make_unique<RenderBuilderUISystem>());  // Path confirm UI
    sm.register_render_system(
        std::make_unique<RenderBuildToolUISystem>());  // Current tool
    sm.register_render_system(
        std::make_unique<RenderBuildToolsPanelSystem>());  // Tools panel
    sm.register_render_system(std::make_unique<RenderUISystem>());
    sm.register_render_system(std::make_unique<EndRenderSystem>());
}
