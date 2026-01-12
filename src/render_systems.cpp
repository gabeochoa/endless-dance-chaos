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
    void for_each_with(const Entity&, const Transform& t, const Agent& a, const HasStress& s, float) const override {
        raylib::Color color = raylib::ColorLerp(raylib::GRAY, raylib::RED, s.stress);
        
        float jitter_x = 0.f, jitter_z = 0.f;
        if (s.stress > 0.7f) {
            float jitter_strength = (s.stress - 0.7f) * 0.3f;
            jitter_x = ((rand() % 100) / 100.f - 0.5f) * jitter_strength;
            jitter_z = ((rand() % 100) / 100.f - 0.5f) * jitter_strength;
        }
        
        raylib::Vector3 pos = {t.position.x + jitter_x, t.radius, t.position.y + jitter_z};
        
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
        
        raylib::DrawCube(vec::to3(t.position), 1.5f, 0.5f, 1.5f, color);
        raylib::DrawCubeWires(vec::to3(t.position), 1.5f, 0.5f, 1.5f, wire_color);
        
        float fill_pct = (float)f.current_occupants / (float)f.capacity;
        float fill_height = fill_pct * 1.0f;
        if (fill_height > 0.01f) {
            raylib::Vector3 fill_pos = {t.position.x, 0.5f + fill_height * 0.5f, t.position.y};
            raylib::DrawCube(fill_pos, 1.4f, fill_height, 1.4f, raylib::Fade(wire_color, 0.6f));
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
    sm.register_render_system(std::make_unique<RenderUISystem>());
    sm.register_render_system(std::make_unique<EndRenderSystem>());
}
