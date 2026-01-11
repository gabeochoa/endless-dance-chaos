
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
    
    // Create an attraction (spawns agents)
    Entity& attraction = EntityHelper::createEntity();
    attraction.addComponent<Transform>(-5.f, -5.f);
    attraction.addComponent<Attraction>();
    
    // Create a facility (absorbs agents)
    Entity& facility = EntityHelper::createEntity();
    facility.addComponent<Transform>(5.f, 5.f);
    facility.addComponent<Facility>();
    
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
