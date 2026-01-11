
#include "game.h"
#include "components.h"
#include "systems.h"
#include "mcp_integration.h"
#include <argh.h>

bool running = true;

using namespace afterhours;

void game() {
    SystemManager systems;
    register_all_systems(systems);
    
    Entity& cam_entity = EntityHelper::createPermanentEntity();
    cam_entity.addComponent<ProvidesCamera>();
    EntityHelper::registerSingleton<ProvidesCamera>(cam_entity);
    
    // Create a facility (goal for agents)
    Entity& facility = EntityHelper::createEntity();
    facility.addComponent<Transform>(5.f, 5.f);
    facility.addComponent<Facility>();
    
    // Spawn multiple agents in a cluster
    for (int i = 0; i < 20; i++) {
        Entity& agent = EntityHelper::createEntity();
        float x = -3.f + (i % 5) * 0.5f;
        float z = -3.f + (i / 5) * 0.5f;
        agent.addComponent<Transform>(x, z);
        agent.addComponent<Agent>();
        agent.addComponent<BoidsBehavior>();
    }
    
    EntityHelper::merge_entity_arrays();
    
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
    
    if (cmdl[{"--mcp"}]) {
        mcp_integration::init();
    }
    
    game();
    
    mcp_integration::shutdown();
    raylib::CloseWindow();
    
    log_info("Goodbye!");
    return 0;
}
