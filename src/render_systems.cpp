#include "systems.h"
#include "components.h"
#include "vec_util.h"
#include "afterhours/src/core/entity_helper.h"

struct BeginRenderSystem : System<> {
    void once(float) const override {
        raylib::BeginDrawing();
        raylib::ClearBackground(raylib::Color{40, 44, 52, 255});
        
        auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
        if (cam) {
            raylib::BeginMode3D(cam->cam.camera);
        }
    }
};

struct RenderGroundSystem : System<> {
    void once(float) const override {
        raylib::Color gridColor = raylib::Fade(raylib::DARKGRAY, 0.5f);
        int size = 10;
        float spacing = TILESIZE;
        
        for (int i = -size; i <= size; i++) {
            raylib::DrawLine3D(
                {(float)i * spacing, 0.0f, (float)-size * spacing},
                {(float)i * spacing, 0.0f, (float)size * spacing},
                gridColor);
            raylib::DrawLine3D(
                {(float)-size * spacing, 0.0f, (float)i * spacing},
                {(float)size * spacing, 0.0f, (float)i * spacing},
                gridColor);
        }
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
        raylib::DrawSphere({t.position.x, 0.1f, t.position.y}, 0.15f, raylib::SKYBLUE);
        
        if (node.next_node_id < 0) return;
        
        auto next = EntityHelper::getEntityForID(node.next_node_id);
        if (!next.valid() || !next->has<Transform>()) return;
        
        const Transform& next_t = next->get<Transform>();
        vec2 a = t.position;
        vec2 b = next_t.position;
        
        raylib::DrawLine3D(
            {a.x, 0.05f, a.y},
            {b.x, 0.05f, b.y},
            raylib::Fade(raylib::SKYBLUE, 0.7f));
        
        vec2 dir = vec::norm({b.x - a.x, b.y - a.y});
        vec2 perp = {-dir.y, dir.x};
        float hw = node.width * 0.5f;
        
        raylib::DrawLine3D(
            {a.x + perp.x * hw, 0.02f, a.y + perp.y * hw},
            {b.x + perp.x * hw, 0.02f, b.y + perp.y * hw},
            raylib::Fade(raylib::SKYBLUE, 0.3f));
        raylib::DrawLine3D(
            {a.x - perp.x * hw, 0.02f, a.y - perp.y * hw},
            {b.x - perp.x * hw, 0.02f, b.y - perp.y * hw},
            raylib::Fade(raylib::SKYBLUE, 0.3f));
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
