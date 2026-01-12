#include "systems.h"
#include "components.h"
#include "vec_util.h"
#include "afterhours/src/core/entity_helper.h"

// Render texture from main.cpp for MCP screenshots
extern raylib::RenderTexture2D g_render_texture;

struct BeginRenderSystem : System<> {
    void once(float) const override {
        // Render to texture for MCP screenshot support
        raylib::BeginTextureMode(g_render_texture);
        raylib::ClearBackground(raylib::Color{40, 44, 52, 255});
        
        auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
        if (cam) {
            raylib::BeginMode3D(cam->cam.camera);
        }
    }
};

struct RenderGroundSystem : System<> {
    void once(float) const override {
        // Draw grass ground plane
        float size = 10.0f * TILESIZE;
        raylib::Color grassColor = {58, 95, 40, 255};  // Deep grass green
        
        // Main grass plane
        raylib::DrawPlane({0.0f, 0.0f, 0.0f}, {size * 2.0f, size * 2.0f}, grassColor);
        
        // TODO: Render individual grass blades for visual detail
        // Could use instanced rendering with small quads or billboards
        // varying height, slight color variation, and gentle wind sway
    }
};

struct RenderAgentsSystem : System<Transform, Agent, HasStress> {
    void for_each_with(const Entity& e, const Transform& t, const Agent& a, const HasStress& s, float) const override {
        raylib::Color color = raylib::ColorLerp(raylib::GRAY, raylib::RED, s.stress);
        
        float jitter_x = 0.f, jitter_z = 0.f;
        if (s.stress > 0.7f) {
            float jitter_strength = (s.stress - 0.7f) * 0.3f;
            jitter_x = ((rand() % 100) / 100.f - 0.5f) * jitter_strength;
            jitter_z = ((rand() % 100) / 100.f - 0.5f) * jitter_strength;
        }
        
        raylib::Vector3 pos = {t.position.x + jitter_x, t.radius, t.position.y + jitter_z};
        
        // If inside a facility, offset position to slot location
        if (e.has<InsideFacility>()) {
            const InsideFacility& inside = e.get<InsideFacility>();
            pos.x += inside.slot_offset.x;
            pos.z += inside.slot_offset.y;
        }
        
        switch (a.want) {
            case FacilityType::Stage:
                raylib::DrawSphere(pos, t.radius, color);
                break;
            case FacilityType::Food:
                raylib::DrawCylinder(pos, 0.f, t.radius, t.radius * 2.f, 4, color);
                break;
            case FacilityType::Bathroom:
                raylib::DrawCube(pos, t.radius * 1.5f, t.radius * 1.5f, t.radius * 1.5f, color);
                break;
        }
        
        if (s.stress > 0.9f) {
            for (int i = 0; i < 3; i++) {
                float spark_x = pos.x + ((rand() % 100) / 100.f - 0.5f) * 0.5f;
                float spark_y = pos.y + ((rand() % 100) / 100.f) * 0.5f;
                float spark_z = pos.z + ((rand() % 100) / 100.f - 0.5f) * 0.5f;
                raylib::DrawSphere({spark_x, spark_y, spark_z}, 0.03f, raylib::YELLOW);
            }
        }
    }
};

struct RenderAttractionsSystem : System<Transform, Attraction> {
    void for_each_with(const Entity&, const Transform& t, const Attraction&, float) const override {
        raylib::DrawCube(vec::to3(t.position), 1.0f, 0.5f, 1.0f, raylib::PURPLE);
        raylib::DrawCubeWires(vec::to3(t.position), 1.0f, 0.5f, 1.0f, raylib::DARKPURPLE);
    }
};

struct RenderFacilitiesSystem : System<Transform, Facility> {
    static constexpr float FACILITY_SIZE = 1.5f;
    static constexpr float FACILITY_HEIGHT = 0.5f;
    
    void for_each_with(const Entity&, const Transform& t, const Facility& f, float) const override {
        raylib::Color color, wire_color;
        switch (f.type) {
            case FacilityType::Bathroom:
                color = raylib::BLUE;
                wire_color = raylib::DARKBLUE;
                break;
            case FacilityType::Food:
                color = raylib::YELLOW;
                wire_color = raylib::ORANGE;
                break;
            case FacilityType::Stage:
                color = raylib::MAGENTA;
                wire_color = raylib::DARKPURPLE;
                break;
        }
        
        raylib::Vector3 pos = vec::to3(t.position);
        pos.y = FACILITY_HEIGHT * 0.5f;
        
        if (f.type == FacilityType::Stage) {
            // Stage: wireframe only so we can see people inside
            raylib::DrawCubeWires(pos, FACILITY_SIZE, FACILITY_HEIGHT, FACILITY_SIZE, wire_color);
            // Draw a floor
            raylib::DrawPlane({t.position.x, 0.02f, t.position.y}, 
                             {FACILITY_SIZE, FACILITY_SIZE}, 
                             raylib::Fade(color, 0.3f));
            // State indicator rendered as 2D overlay by RenderStageIndicatorsSystem
        } else {
            // Other facilities: draw 3 walls (open on entry side - negative X)
            float hs = FACILITY_SIZE * 0.5f;  // half size
            float hh = FACILITY_HEIGHT * 0.5f;  // half height
            float wall_thick = 0.05f;
            
            // Back wall (positive X side)
            raylib::DrawCube({t.position.x + hs, hh, t.position.y}, 
                            wall_thick, FACILITY_HEIGHT, FACILITY_SIZE, color);
            // Left wall (negative Z side)  
            raylib::DrawCube({t.position.x, hh, t.position.y - hs}, 
                            FACILITY_SIZE, FACILITY_HEIGHT, wall_thick, color);
            // Right wall (positive Z side)
            raylib::DrawCube({t.position.x, hh, t.position.y + hs}, 
                            FACILITY_SIZE, FACILITY_HEIGHT, wall_thick, color);
            
            // Draw floor
            raylib::DrawPlane({t.position.x, 0.02f, t.position.y}, 
                             {FACILITY_SIZE, FACILITY_SIZE}, 
                             raylib::Fade(color, 0.3f));
            
            // Wireframe outline
            raylib::DrawCubeWires(pos, FACILITY_SIZE, FACILITY_HEIGHT, FACILITY_SIZE, wire_color);
        }
    }
};

struct RenderPathsSystem : System<Transform, PathNode> {
    void for_each_with(const Entity&, const Transform& t, const PathNode& node, float) const override {
        // Sidewalk colors
        raylib::Color sidewalkMain = {180, 175, 165, 255};    // Warm concrete gray
        raylib::Color sidewalkEdge = {140, 135, 125, 255};    // Darker edge/border
        raylib::Color sidewalkCrack = {120, 115, 105, 255};   // Crack/joint lines
        
        if (node.next_node_id < 0) {
            // Dead-end node - draw a small circular pad
            raylib::DrawCylinder({t.position.x, 0.01f, t.position.y}, 
                                 node.width * 0.5f, node.width * 0.5f, 0.02f, 16, sidewalkMain);
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
        
        // Draw main sidewalk surface as two triangles (both sides for visibility)
        raylib::DrawTriangle3D(v1, v2, v3, sidewalkMain);
        raylib::DrawTriangle3D(v3, v2, v1, sidewalkMain);  // Back face
        raylib::DrawTriangle3D(v1, v3, v4, sidewalkMain);
        raylib::DrawTriangle3D(v4, v3, v1, sidewalkMain);  // Back face
        
        // Draw sidewalk edges (borders)
        raylib::Vector3 e1a = {a.x + perp.x * hw, y + 0.005f, a.y + perp.y * hw};
        raylib::Vector3 e1b = {b.x + perp.x * hw, y + 0.005f, b.y + perp.y * hw};
        raylib::Vector3 e2a = {a.x - perp.x * hw, y + 0.005f, a.y - perp.y * hw};
        raylib::Vector3 e2b = {b.x - perp.x * hw, y + 0.005f, b.y - perp.y * hw};
        
        raylib::DrawLine3D(e1a, e1b, sidewalkEdge);
        raylib::DrawLine3D(e2a, e2b, sidewalkEdge);
        
        // Draw expansion joint lines across the sidewalk (every ~1 unit)
        float segLen = vec::length({b.x - a.x, b.y - a.y});
        int numJoints = (int)(segLen / 1.0f);
        for (int i = 1; i < numJoints; i++) {
            float t_joint = (float)i / (float)numJoints;
            vec2 jointPos = {a.x + (b.x - a.x) * t_joint, a.y + (b.y - a.y) * t_joint};
            raylib::Vector3 j1 = {jointPos.x + perp.x * hw, y + 0.005f, jointPos.y + perp.y * hw};
            raylib::Vector3 j2 = {jointPos.x - perp.x * hw, y + 0.005f, jointPos.y - perp.y * hw};
            raylib::DrawLine3D(j1, j2, sidewalkCrack);
        }
    }
};

struct EndMode3DSystem : System<> {
    void once(float) const override {
        auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
        if (cam) {
            raylib::EndMode3D();
        }
    }
};

// Renders circular progress indicators above stages (2D overlay)
struct RenderStageIndicatorsSystem : System<Transform, Facility, StageInfo> {
    static constexpr float INDICATOR_RADIUS = 30.0f;
    
    void for_each_with(const Entity&, const Transform& t, const Facility&, 
                       const StageInfo& info, float) const override {
        auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
        if (!cam) return;
        
        // Project 3D position to screen space
        raylib::Vector3 world_pos = {t.position.x, 1.5f, t.position.y};  // Above the stage
        raylib::Vector2 screen_pos = raylib::GetWorldToScreen(world_pos, cam->cam.camera);
        
        // Skip if off screen
        if (screen_pos.x < -50 || screen_pos.x > DEFAULT_SCREEN_WIDTH + 50 ||
            screen_pos.y < -50 || screen_pos.y > DEFAULT_SCREEN_HEIGHT + 50) return;
        
        float radius = INDICATOR_RADIUS;
        float progress = info.get_progress();
        
        // Pick colors based on state
        raylib::Color bg_color, fg_color, icon_color;
        const char* icon_char = "?";
        
        switch (info.state) {
            case StageState::Idle:
                bg_color = raylib::Color{60, 60, 60, 200};
                fg_color = raylib::Color{100, 100, 100, 220};
                icon_color = raylib::Color{150, 150, 150, 255};
                icon_char = "Z";  // Sleeping/idle
                break;
            case StageState::Announcing:
                bg_color = raylib::Color{80, 60, 20, 200};
                fg_color = raylib::Color{255, 200, 50, 230};
                icon_color = raylib::Color{255, 230, 100, 255};
                icon_char = "!";  // Alert/upcoming
                break;
            case StageState::Performing:
                bg_color = raylib::Color{30, 80, 30, 200};
                fg_color = raylib::Color{80, 220, 80, 230};
                icon_color = raylib::Color{150, 255, 150, 255};
                icon_char = "*";  // Music/performing (♫ not reliably rendered)
                break;
            case StageState::Clearing:
                bg_color = raylib::Color{80, 30, 30, 200};
                fg_color = raylib::Color{220, 60, 60, 230};
                icon_color = raylib::Color{255, 100, 100, 255};
                icon_char = "X";  // Clearing out
                break;
        }
        
        // Background circle
        raylib::DrawCircle((int)screen_pos.x, (int)screen_pos.y, radius, bg_color);
        
        // Progress arc - starts at top (-90°), sweeps clockwise
        float start_angle = -90.0f;
        float end_angle = start_angle + (360.0f * progress);
        raylib::DrawCircleSector(screen_pos, radius - 3, start_angle, end_angle, 32, fg_color);
        
        // Progress line showing current position
        if (progress > 0.01f && progress < 0.99f) {
            float angle_rad = end_angle * 0.0174533f;  // DEG2RAD
            float line_x = screen_pos.x + (radius - 3) * cosf(angle_rad);
            float line_y = screen_pos.y + (radius - 3) * sinf(angle_rad);
            raylib::DrawLine((int)screen_pos.x, (int)screen_pos.y, 
                            (int)line_x, (int)line_y, raylib::WHITE);
        }
        
        // Inner circle for icon background
        raylib::DrawCircle((int)screen_pos.x, (int)screen_pos.y, radius * 0.5f, 
                          raylib::Color{30, 30, 30, 230});
        
        // Icon character (centered)
        int font_size = (int)(radius * 0.7f);
        int text_width = raylib::MeasureText(icon_char, font_size);
        raylib::DrawText(icon_char, 
                        (int)(screen_pos.x - text_width / 2), 
                        (int)(screen_pos.y - font_size / 2),
                        font_size, icon_color);
        
        // Outer ring
        raylib::DrawCircleLines((int)screen_pos.x, (int)screen_pos.y, radius, 
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
        raylib::DrawTextureRec(
            g_render_texture.texture,
            {0, 0, (float)g_render_texture.texture.width, -(float)g_render_texture.texture.height},
            {0, 0},
            raylib::WHITE
        );
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
