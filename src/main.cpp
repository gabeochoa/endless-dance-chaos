
#include "game.h"
#include "systems.h"
#include "entity_makers.h"
#include "mcp_integration.h"
#include <argh.h>

bool running = true;

// Render texture for MCP screenshots
raylib::RenderTexture2D g_render_texture;

using namespace afterhours;

void game() {
    SystemManager systems;
    register_all_systems(systems);
    
    make_camera();
    
    // Create an attraction (spawns agents)
    make_attraction(-5.f, -5.f, 5.0f, 100);
    
    // Create facilities
    make_bathroom(5.f, -3.f);
    make_food(5.f, 0.f);
    make_stage(5.f, 3.f);
    
    // Path to bathroom (bottom)
    Entity& bath_end = make_path_node(5.f, -3.f);
    Entity& bath_mid = make_path_node(0.f, -3.f, bath_end.id);
    
    // Path to food (middle)
    Entity& food_end = make_path_node(5.f, 0.f);
    Entity& food_mid = make_path_node(0.f, 0.f, food_end.id);
    
    // Path to stage (top)
    Entity& stage_end = make_path_node(5.f, 3.f);
    Entity& stage_mid = make_path_node(0.f, 3.f, stage_end.id);
    
    // Central hub connecting attraction to all paths
    Entity& hub = make_path_node(-3.f, 0.f, food_mid.id);
    make_path_node(-3.f, 0.f, bath_mid.id);  // hub_to_bath (co-located with hub)
    make_path_node(-3.f, 0.f, stage_mid.id); // hub_to_stage (co-located with hub)
    
    // Path from attraction to hub
    make_path_node(-5.f, -5.f, hub.id);
    
    EntityHelper::merge_entity_arrays();
    
    // Calculate signposts for all path nodes
    calculate_path_signposts();
    
    while (running && !raylib::WindowShouldClose()) {
        float dt = raylib::GetFrameTime();
        systems.run(dt);
    }
}

int main(int argc, char *argv[]) {
    argh::parser cmdl(argc, argv, argh::parser::PREFER_PARAM_FOR_UNREG_OPTION);
    
    log_info("Starting Endless Dance Chaos v{}", VERSION);
    
    raylib::InitWindow(DEFAULT_SCREEN_WIDTH, DEFAULT_SCREEN_HEIGHT, "Endless Dance Chaos");
    raylib::SetTargetFPS(60);
    
    // Create render texture for MCP screenshots
    g_render_texture = raylib::LoadRenderTexture(DEFAULT_SCREEN_WIDTH, DEFAULT_SCREEN_HEIGHT);
    
    if (cmdl[{"--mcp"}]) {
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
