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

struct RenderAgentsSystem : System<Transform, Agent> {
    void for_each_with(const Entity&, const Transform& t, const Agent&, float) const override {
        raylib::DrawSphere(vec::to3(t.position), t.radius, raylib::ORANGE);
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
    sm.register_render_system(std::make_unique<RenderAgentsSystem>());
    sm.register_render_system(std::make_unique<EndMode3DSystem>());
    sm.register_render_system(std::make_unique<RenderUISystem>());
    sm.register_render_system(std::make_unique<EndRenderSystem>());
}
