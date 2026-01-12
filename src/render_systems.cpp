#include "afterhours/src/core/entity_helper.h"
#include "components.h"
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

struct RenderPathsSystem : System<Transform, PathNode> {
    static constexpr raylib::Color SIDEWALK_MAIN = {180, 175, 165, 255};
    static constexpr raylib::Color SIDEWALK_EDGE = {140, 135, 125, 255};
    static constexpr raylib::Color SIDEWALK_CRACK = {120, 115, 105, 255};

    void for_each_with(const Entity&, const Transform& t, const PathNode& node,
                       float) const override {
        if (node.next_node_id < 0) {
            // Dead-end node - draw a small circular pad
            raylib::DrawCylinder({t.position.x, 0.01f, t.position.y},
                                 node.width * 0.5f, node.width * 0.5f, 0.02f,
                                 16, SIDEWALK_MAIN);
            return;
        }

        auto next = EntityHelper::getEntityForID(node.next_node_id);
        if (!next.valid() || !next->has<Transform>()) return;

        const Transform& next_t = next->get<Transform>();
        vec2 a = t.position;
        vec2 b = next_t.position;

        vec2 dir = vec::norm({b.x - a.x, b.y - a.y});
        vec2 perp = {-dir.y, dir.x};
        float hw = node.width * 0.5f;
        float y = 0.01f;  // Slightly above ground to prevent z-fighting

        // Calculate the four corners of the sidewalk segment
        raylib::Vector3 v1 = {a.x + perp.x * hw, y, a.y + perp.y * hw};
        raylib::Vector3 v2 = {a.x - perp.x * hw, y, a.y - perp.y * hw};
        raylib::Vector3 v3 = {b.x - perp.x * hw, y, b.y - perp.y * hw};
        raylib::Vector3 v4 = {b.x + perp.x * hw, y, b.y + perp.y * hw};

        // Draw main sidewalk surface as two triangles (both sides for
        // visibility)
        raylib::DrawTriangle3D(v1, v2, v3, SIDEWALK_MAIN);
        raylib::DrawTriangle3D(v3, v2, v1, SIDEWALK_MAIN);  // Back face
        raylib::DrawTriangle3D(v1, v3, v4, SIDEWALK_MAIN);
        raylib::DrawTriangle3D(v4, v3, v1, SIDEWALK_MAIN);  // Back face

        // Draw sidewalk edges (borders)
        raylib::Vector3 e1a = {a.x + perp.x * hw, y + 0.005f,
                               a.y + perp.y * hw};
        raylib::Vector3 e1b = {b.x + perp.x * hw, y + 0.005f,
                               b.y + perp.y * hw};
        raylib::Vector3 e2a = {a.x - perp.x * hw, y + 0.005f,
                               a.y - perp.y * hw};
        raylib::Vector3 e2b = {b.x - perp.x * hw, y + 0.005f,
                               b.y - perp.y * hw};

        raylib::DrawLine3D(e1a, e1b, SIDEWALK_EDGE);
        raylib::DrawLine3D(e2a, e2b, SIDEWALK_EDGE);

        // Draw expansion joint lines across the sidewalk (every ~1 unit)
        float segLen = vec::length({b.x - a.x, b.y - a.y});
        int numJoints = (int) (segLen / 1.0f);
        for (int i = 1; i < numJoints; i++) {
            float t_joint = (float) i / (float) numJoints;
            vec2 jointPos = {a.x + (b.x - a.x) * t_joint,
                             a.y + (b.y - a.y) * t_joint};
            raylib::Vector3 j1 = {jointPos.x + perp.x * hw, y + 0.005f,
                                  jointPos.y + perp.y * hw};
            raylib::Vector3 j2 = {jointPos.x - perp.x * hw, y + 0.005f,
                                  jointPos.y - perp.y * hw};
            raylib::DrawLine3D(j1, j2, SIDEWALK_CRACK);
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

struct RenderUISystem : System<> {
    void once(float) const override {
        raylib::DrawText("Endless Dance Chaos", 10, 10, 20, raylib::WHITE);
        raylib::DrawFPS(DEFAULT_SCREEN_WIDTH - 100, 10);
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
    sm.register_render_system(std::make_unique<RenderAttractionsSystem>());
    sm.register_render_system(std::make_unique<RenderFacilitiesSystem>());
    sm.register_render_system(std::make_unique<RenderAgentsSystem>());
    sm.register_render_system(std::make_unique<EndMode3DSystem>());
    sm.register_render_system(std::make_unique<RenderStageIndicatorsSystem>());
    sm.register_render_system(std::make_unique<RenderUISystem>());
    sm.register_render_system(std::make_unique<EndRenderSystem>());
}
