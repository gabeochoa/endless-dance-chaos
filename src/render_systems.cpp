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
    void for_each_with(const Entity&, const Transform& t, const Agent&, const HasStress& s, float) const override {
        raylib::Color color = raylib::ColorLerp(raylib::ORANGE, raylib::RED, s.stress);
        raylib::DrawSphere(vec::to3(t.position), t.radius, color);
    }
};

struct RenderAttractionsSystem : System<Transform, Attraction> {
    void for_each_with(const Entity&, const Transform& t, const Attraction&, float) const override {
        raylib::DrawCube(vec::to3(t.position), 1.0f, 0.5f, 1.0f, raylib::PURPLE);
        raylib::DrawCubeWires(vec::to3(t.position), 1.0f, 0.5f, 1.0f, raylib::DARKPURPLE);
    }
};

struct RenderFacilitiesSystem : System<Transform, Facility> {
    void for_each_with(const Entity&, const Transform& t, const Facility&, float) const override {
        raylib::DrawCube(vec::to3(t.position), 1.0f, 0.5f, 1.0f, raylib::GREEN);
        raylib::DrawCubeWires(vec::to3(t.position), 1.0f, 0.5f, 1.0f, raylib::DARKGREEN);
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
