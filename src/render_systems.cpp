// AFTER_HOURS_REPLACE_LOGGING and log.h must come first
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "afterhours/src/core/entity_query.h"
#include "components.h"
#include "entity_makers.h"
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

// Render agents as small person-shaped primitives in 3D space
struct RenderAgentsSystem : System<Agent, Transform> {
    static constexpr raylib::Color AGENT_COLOR = {212, 165, 116,
                                                  255};  // warm tan

    void for_each_with(Entity& e, Agent&, Transform& tf, float) override {
        // Hide agents being serviced inside a facility
        if (!e.is_missing<BeingServiced>()) return;

        // Agent position in world space (x = world x, y = world z)
        float wx = tf.position.x;
        float wz = tf.position.y;

        // Draw a small capsule-like shape: sphere head + cube body
        float body_h = 0.3f;
        float head_r = 0.1f;

        // Body (small cube standing on the ground)
        raylib::DrawCube({wx, body_h * 0.5f, wz}, 0.2f, body_h, 0.2f,
                         AGENT_COLOR);
        // Head (sphere on top of body)
        raylib::DrawSphere({wx, body_h + head_r, wz}, head_r, AGENT_COLOR);
    }
};

// Render path-drawing preview overlays on the grid
struct RenderBuildPreviewSystem : System<> {
    // Semi-transparent green for valid new path tiles
    static constexpr raylib::Color PREVIEW_VALID = {100, 220, 130, 100};
    // Semi-transparent gray for tiles already path
    static constexpr raylib::Color PREVIEW_EXISTING = {180, 180, 180, 80};
    // Hover cursor (white outline plane)
    static constexpr raylib::Color HOVER_NORMAL = {255, 255, 255, 120};
    // Red hover in demolish mode
    static constexpr raylib::Color HOVER_DEMOLISH = {255, 60, 60, 140};

    void once(float) const override {
        auto* pds = EntityHelper::get_singleton_cmp<PathDrawState>();
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!pds || !grid || !pds->hover_valid) return;

        float tile_size = TILESIZE * 0.98f;
        float preview_y = 0.03f;  // Slightly above tiles

        // Draw rectangle preview while drawing
        if (pds->is_drawing) {
            int min_x, min_z, max_x, max_z;
            pds->get_rect(min_x, min_z, max_x, max_z);

            for (int z = min_z; z <= max_z; z++) {
                for (int x = min_x; x <= max_x; x++) {
                    if (!grid->in_bounds(x, z)) continue;
                    bool already_path = grid->at(x, z).type == TileType::Path;
                    raylib::Color color =
                        already_path ? PREVIEW_EXISTING : PREVIEW_VALID;
                    raylib::DrawPlane({x * TILESIZE, preview_y, z * TILESIZE},
                                      {tile_size, tile_size}, color);
                }
            }
        }

        // Draw hover cursor
        raylib::Color cursor_color =
            pds->demolish_mode ? HOVER_DEMOLISH : HOVER_NORMAL;
        raylib::DrawPlane(
            {pds->hover_x * TILESIZE, preview_y, pds->hover_z * TILESIZE},
            {tile_size, tile_size}, cursor_color);
    }
};

struct EndMode3DSystem : System<> {
    void once(float) const override {
        auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
        if (cam) raylib::EndMode3D();
    }
};

// Update hover grid position from mouse (runs in render phase after EndMode3D
// because GetWorldToScreen needs internal raylib state from the 3D pass)
struct HoverTrackingSystem : System<> {
    void once(float) const override {
        auto* pds = EntityHelper::get_singleton_cmp<PathDrawState>();
        auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!pds || !cam || !grid) return;

        // Use afterhours input API so E2E injected positions are respected
        auto mouse = input::get_mouse_position();
        auto result = cam->cam.screen_to_grid(mouse.x, mouse.y);

        if (result.has_value()) {
            auto [gx, gz] = result.value();
            pds->hover_x = gx;
            pds->hover_z = gz;
            pds->hover_valid = grid->in_bounds(gx, gz);
        } else {
            pds->hover_valid = false;
        }
    }
};

struct RenderUISystem : System<> {
    void once(float) const override {
        // Title
        raylib::DrawText("Endless Dance Chaos", 10, 10, 20, raylib::WHITE);
        raylib::DrawFPS(DEFAULT_SCREEN_WIDTH - 100, 10);

        // Build mode indicator
        auto* pds = EntityHelper::get_singleton_cmp<PathDrawState>();
        if (pds) {
            if (pds->demolish_mode) {
                raylib::DrawText("DEMOLISH MODE [X]", 10, 40, 18,
                                 raylib::Color{255, 80, 80, 255});
            } else if (pds->is_drawing) {
                raylib::DrawText(
                    "Drawing path... (click to confirm, "
                    "right-click to cancel)",
                    10, 40, 16, raylib::Color{180, 255, 180, 255});
            } else {
                raylib::DrawText("Build Path [click to start] | [X] Demolish",
                                 10, 40, 16, raylib::Color{200, 200, 200, 200});
            }

            // Grid position readout
            if (pds->hover_valid) {
                std::string hover_text =
                    fmt::format("Grid: ({}, {})", pds->hover_x, pds->hover_z);
                raylib::DrawText(hover_text.c_str(), 10, 60, 14,
                                 raylib::Color{180, 180, 180, 200});
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
    sm.register_render_system(std::make_unique<RenderGridSystem>());
    sm.register_render_system(std::make_unique<RenderAgentsSystem>());
    sm.register_render_system(std::make_unique<RenderBuildPreviewSystem>());
    sm.register_render_system(std::make_unique<EndMode3DSystem>());
    sm.register_render_system(std::make_unique<HoverTrackingSystem>());
    sm.register_render_system(std::make_unique<RenderUISystem>());
    sm.register_render_system(std::make_unique<EndRenderSystem>());
}
