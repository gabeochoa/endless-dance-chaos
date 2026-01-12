
#include <argh.h>

#include "entity_makers.h"
#include "game.h"
#include "mcp_integration.h"
#include "systems.h"

bool running = true;

// Render texture for MCP screenshots
raylib::RenderTexture2D g_render_texture;

using namespace afterhours;

void game() {
    SystemManager systems;
    register_all_systems(systems);

    make_sophie();

    // Create an attraction (spawns agents)
    make_attraction(-5.f, -5.f, 5.0f, 100);

    // Create facilities
    make_bathroom(5.f, -3.f);
    make_food(5.f, 0.f);
    make_stage(5.f, 3.f);

    // Create initial path layout using grid tiles
    create_initial_path_layout();

    EntityHelper::merge_entity_arrays();

    // Calculate signposts for path tiles
    calculate_path_signposts();

    while (running && !raylib::WindowShouldClose()) {
        float dt = raylib::GetFrameTime();
        systems.run(dt);
    }
}

int main(int argc, char* argv[]) {
    argh::parser cmdl(argc, argv, argh::parser::PREFER_PARAM_FOR_UNREG_OPTION);

    bool mcp_mode = cmdl[{"--mcp"}];

    // In MCP mode, suppress raylib logs and redirect our logs to stderr
    if (mcp_mode) {
        raylib::SetTraceLogLevel(raylib::LOG_NONE);
        g_log_to_stderr = true;
    }

    log_info("Starting Endless Dance Chaos v{}", VERSION);

    raylib::InitWindow(DEFAULT_SCREEN_WIDTH, DEFAULT_SCREEN_HEIGHT,
                       "Endless Dance Chaos");
    raylib::SetTargetFPS(60);

    // Create render texture for MCP screenshots
    g_render_texture =
        raylib::LoadRenderTexture(DEFAULT_SCREEN_WIDTH, DEFAULT_SCREEN_HEIGHT);

    if (mcp_mode) {
        mcp_integration::set_screenshot_texture(&g_render_texture);
        mcp_integration::init();
    }

    game();

    mcp_integration::shutdown();
    raylib::UnloadRenderTexture(g_render_texture);
    raylib::CloseWindow();

    log_info("Goodbye!");
    return 0;
}
