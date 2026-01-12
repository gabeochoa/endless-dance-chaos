
#include "game.h"
#include "components.h"
#include "systems.h"
#include "mcp_integration.h"
#include <argh.h>

bool running = true;

// Render texture for MCP screenshots
raylib::RenderTexture2D g_render_texture;

using namespace afterhours;

void game() {
    SystemManager systems;
    register_all_systems(systems);
    
    Entity& cam_entity = EntityHelper::createPermanentEntity();
    cam_entity.addComponent<ProvidesCamera>();
    EntityHelper::registerSingleton<ProvidesCamera>(cam_entity);
    
    // Create an attraction (spawns agents)
    Entity& attraction = EntityHelper::createEntity();
    attraction.addComponent<Transform>(-5.f, -5.f);
    Attraction& attr = attraction.addComponent<Attraction>();
    attr.spawn_rate = 5.0f;
    attr.capacity = 100;
    
    // Create facilities of each type
    Entity& bathroom = EntityHelper::createEntity();
    bathroom.addComponent<Transform>(5.f, -3.f);
    bathroom.addComponent<Facility>().type = FacilityType::Bathroom;
    
    Entity& food = EntityHelper::createEntity();
    food.addComponent<Transform>(5.f, 0.f);
    food.addComponent<Facility>().type = FacilityType::Food;
    
    Entity& stage = EntityHelper::createEntity();
    stage.addComponent<Transform>(5.f, 3.f);
    stage.addComponent<Facility>().type = FacilityType::Stage;
    stage.addComponent<StageInfo>();  // Stage state machine
    
    // Path to bathroom (bottom)
    Entity& bath_end = EntityHelper::createEntity();
    bath_end.addComponent<Transform>(5.f, -3.f);
    bath_end.addComponent<PathNode>().next_node_id = -1;
    
    Entity& bath_mid = EntityHelper::createEntity();
    bath_mid.addComponent<Transform>(0.f, -3.f);
    bath_mid.addComponent<PathNode>().next_node_id = bath_end.id;
    
    // Path to food (middle)
    Entity& food_end = EntityHelper::createEntity();
    food_end.addComponent<Transform>(5.f, 0.f);
    food_end.addComponent<PathNode>().next_node_id = -1;
    
    Entity& food_mid = EntityHelper::createEntity();
    food_mid.addComponent<Transform>(0.f, 0.f);
    food_mid.addComponent<PathNode>().next_node_id = food_end.id;
    
    // Path to stage (top)
    Entity& stage_end = EntityHelper::createEntity();
    stage_end.addComponent<Transform>(5.f, 3.f);
    stage_end.addComponent<PathNode>().next_node_id = -1;
    
    Entity& stage_mid = EntityHelper::createEntity();
    stage_mid.addComponent<Transform>(0.f, 3.f);
    stage_mid.addComponent<PathNode>().next_node_id = stage_end.id;
    
    // Central hub connecting attraction to all paths
    Entity& hub = EntityHelper::createEntity();
    hub.addComponent<Transform>(-3.f, 0.f);
    hub.addComponent<PathNode>().next_node_id = food_mid.id;
    
    Entity& hub_to_bath = EntityHelper::createEntity();
    hub_to_bath.addComponent<Transform>(-3.f, 0.f);
    hub_to_bath.addComponent<PathNode>().next_node_id = bath_mid.id;
    
    Entity& hub_to_stage = EntityHelper::createEntity();
    hub_to_stage.addComponent<Transform>(-3.f, 0.f);
    hub_to_stage.addComponent<PathNode>().next_node_id = stage_mid.id;
    
    // Path from attraction to hub
    Entity& start = EntityHelper::createEntity();
    start.addComponent<Transform>(-5.f, -5.f);
    start.addComponent<PathNode>().next_node_id = hub.id;
    
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
