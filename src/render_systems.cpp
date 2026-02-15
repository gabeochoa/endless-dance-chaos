// AFTER_HOURS_REPLACE_LOGGING and log.h must come first
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "afterhours/src/core/entity_query.h"
#include "components.h"
#include "game.h"
#include "systems.h"

// Render texture from main.cpp for MCP screenshots
extern raylib::RenderTexture2D g_render_texture;

struct BeginRenderSystem : System<> {
    void once(float) const override {
        // Render to texture for MCP screenshot support
        raylib::BeginTextureMode(g_render_texture);
        raylib::ClearBackground(raylib::Color{40, 44, 52, 255});

        auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
        if (cam) raylib::BeginMode3D(cam->cam.camera);
    }
};

struct RenderGridSystem : System<> {
    static constexpr raylib::Color GRASS_COLOR = {152, 212, 168, 255};
    static constexpr raylib::Color PATH_COLOR = {232, 221, 212, 255};
    static constexpr raylib::Color FENCE_COLOR = {139, 119, 101, 255};
    static constexpr raylib::Color GATE_COLOR = {180, 160, 140, 255};
    static constexpr raylib::Color STAGE_COLOR = {200, 130, 200, 255};
    static constexpr raylib::Color BATHROOM_COLOR = {130, 170, 220, 255};
    static constexpr raylib::Color FOOD_COLOR = {230, 200, 100, 255};

    static raylib::Color tile_color(TileType type) {
        switch (type) {
            case TileType::Grass:
                return GRASS_COLOR;
            case TileType::Path:
                return PATH_COLOR;
            case TileType::Fence:
                return FENCE_COLOR;
            case TileType::Gate:
                return GATE_COLOR;
            case TileType::Stage:
                return STAGE_COLOR;
            case TileType::Bathroom:
                return BATHROOM_COLOR;
            case TileType::Food:
                return FOOD_COLOR;
        }
        return GRASS_COLOR;
    }

    void once(float) const override {
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;

        float tile_size = TILESIZE * 0.98f;

        for (int z = 0; z < MAP_SIZE; z++) {
            for (int x = 0; x < MAP_SIZE; x++) {
                const Tile& tile = grid->at(x, z);
                raylib::Color color = tile_color(tile.type);

                raylib::DrawPlane({x * TILESIZE, 0.01f, z * TILESIZE},
                                  {tile_size, tile_size}, color);
            }
        }
    }
};

struct EndMode3DSystem : System<> {
    void once(float) const override {
        auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
        if (cam) raylib::EndMode3D();
    }
};

struct RenderUISystem : System<> {
    void once(float) const override {
        // Title
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
    sm.register_render_system(std::make_unique<RenderGridSystem>());
    sm.register_render_system(std::make_unique<EndMode3DSystem>());
    sm.register_render_system(std::make_unique<RenderUISystem>());
    sm.register_render_system(std::make_unique<EndRenderSystem>());
}
