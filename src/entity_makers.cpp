#include "entity_makers.h"

#include "afterhours/src/core/entity_query.h"
#include "afterhours/src/plugins/input_system.h"
#include "game.h"
#include "input_mapping.h"

using namespace afterhours;

Entity& make_sophie() {
    Entity& sophie = EntityHelper::createPermanentEntity();

    sophie.addComponent<ProvidesCamera>();
    EntityHelper::registerSingleton<ProvidesCamera>(sophie);

    // Center camera on the grid
    sophie.get<ProvidesCamera>().cam.target = {MAP_SIZE / 2.0f, 0,
                                               MAP_SIZE / 2.0f};
    sophie.get<ProvidesCamera>().cam.update_camera_position();

    sophie.addComponent<Grid>();
    EntityHelper::registerSingleton<Grid>(sophie);

    sophie.addComponent<GameState>();
    EntityHelper::registerSingleton<GameState>(sophie);

    sophie.addComponent<BuilderState>();
    EntityHelper::registerSingleton<BuilderState>(sophie);

    sophie.addComponent<PathDrawState>();
    EntityHelper::registerSingleton<PathDrawState>(sophie);

    sophie.addComponent<SpawnState>();
    EntityHelper::registerSingleton<SpawnState>(sophie);

    // Place pre-built stage (4x4 at STAGE_X, STAGE_Z)
    auto& grid_ref = sophie.get<Grid>();
    for (int z = STAGE_Z; z < STAGE_Z + STAGE_SIZE; z++) {
        for (int x = STAGE_X; x < STAGE_X + STAGE_SIZE; x++) {
            if (grid_ref.in_bounds(x, z)) {
                grid_ref.at(x, z).type = TileType::Stage;
            }
        }
    }

    // Input system
    afterhours::input::add_singleton_components(sophie, get_mapping());

    log_info("Created Sophie entity with all singletons");
    return sophie;
}

Entity& make_agent(int grid_x, int grid_z, FacilityType want, int target_x,
                   int target_z) {
    auto* grid = EntityHelper::get_singleton_cmp<Grid>();

    Entity& e = EntityHelper::createEntity();

    // Convert grid position to world position
    ::vec2 world_pos = grid ? grid->grid_to_world(grid_x, grid_z)
                            : ::vec2{grid_x * TILESIZE, grid_z * TILESIZE};
    e.addComponent<Transform>(world_pos);
    e.addComponent<Agent>(want, target_x, target_z);

    return e;
}

bool should_escape_quit() {
    auto* pds = EntityHelper::get_singleton_cmp<PathDrawState>();
    if (pds && (pds->is_drawing || pds->demolish_mode)) {
        return false;
    }
    return true;
}
