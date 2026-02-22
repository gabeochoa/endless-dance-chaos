#include "entity_makers.h"

#include "afterhours/src/core/entity_query.h"
#include "afterhours/src/plugins/input_system.h"
#include "afterhours/src/plugins/window_manager.h"
#include "engine/random_engine.h"
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

    sophie.addComponent<GameClock>();
    EntityHelper::registerSingleton<GameClock>(sophie);

    sophie.addComponent<ArtistSchedule>();
    EntityHelper::registerSingleton<ArtistSchedule>(sophie);

    sophie.addComponent<FacilitySlots>();
    EntityHelper::registerSingleton<FacilitySlots>(sophie);

    sophie.addComponent<DifficultyState>();
    EntityHelper::registerSingleton<DifficultyState>(sophie);

    sophie.addComponent<HintState>();
    EntityHelper::registerSingleton<HintState>(sophie);

    // Initialize the grid
    auto& grid_ref = sophie.get<Grid>();
    grid_ref.init_perimeter();

    // Window resolution (needed by input system's get_mouse_position)
    using PCR = afterhours::window_manager::ProvidesCurrentResolution;
    sophie.addComponent<PCR>(afterhours::window_manager::Resolution{
        DEFAULT_SCREEN_WIDTH, DEFAULT_SCREEN_HEIGHT});
    EntityHelper::registerSingleton<PCR>(sophie);

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
    e.addComponent<AgentHealth>();

    auto& rng = RandomEngine::get();

    // Random color variety
    e.get<Agent>().color_idx = static_cast<uint8_t>(rng.get_int(0, 7));
    e.addComponent<AgentNeeds>();
    auto& needs = e.get<AgentNeeds>();
    needs.bathroom_threshold = rng.get_float(30.f, 90.f);
    needs.food_threshold = rng.get_float(45.f, 120.f);

    return e;
}

void reset_game_state() {
    // Clear all agents, particles, toasts, and active events
    auto agents = EntityQuery().whereHasComponent<Agent>().gen();
    for (Entity& a : agents) a.cleanup = true;
    auto particles = EntityQuery().whereHasComponent<Particle>().gen();
    for (Entity& p : particles) p.cleanup = true;
    auto toasts = EntityQuery().whereHasComponent<ToastMessage>().gen();
    for (Entity& t : toasts) t.cleanup = true;
    auto events = EntityQuery().whereHasComponent<ActiveEvent>().gen();
    for (Entity& ev : events) ev.cleanup = true;
    auto markers = EntityQuery().whereHasComponent<DeathMarker>().gen();
    for (Entity& m : markers) m.cleanup = true;
    EntityHelper::cleanup();

    // Reset grid
    auto* grid = EntityHelper::get_singleton_cmp<Grid>();
    if (grid) {
        for (auto& tile : grid->tiles) {
            tile.type = TileType::Grass;
            tile.agent_count = 0;
            tile.pheromone = {0, 0, 0, 0, 0};
        }
        grid->init_perimeter();
    }

    // Reset game state
    auto* gs = EntityHelper::get_singleton_cmp<GameState>();
    if (gs) {
        gs->status = GameStatus::Running;
        gs->game_time = 0.f;
        gs->death_count = 0;
        gs->total_agents_served = 0;
        gs->time_survived = 0.f;
        gs->max_attendees = 0;
        gs->show_data_layer = false;
        gs->show_debug = false;
        gs->speed_multiplier = 1.0f;
        gs->agents_exited = 0;
        gs->carryover_count = 0;
    }

    // Reset spawn state
    auto* ss = EntityHelper::get_singleton_cmp<SpawnState>();
    if (ss) {
        ss->interval = DEFAULT_SPAWN_INTERVAL;
        ss->timer = 0.f;
        ss->enabled = true;
        ss->manual_override = false;
    }

    // Reset game clock
    auto* clock = EntityHelper::get_singleton_cmp<GameClock>();
    if (clock) {
        clock->game_time_minutes = 600.0f;  // 10:00am
        clock->speed = GameSpeed::OneX;
        clock->debug_time_mult = 0.f;
    }

    // Reset artist schedule
    auto* sched = EntityHelper::get_singleton_cmp<ArtistSchedule>();
    if (sched) {
        sched->schedule.clear();
        sched->stage_state = StageState::Idle;
        sched->current_artist_idx = -1;
    }

    // Reset hint state
    auto* hs = EntityHelper::get_singleton_cmp<HintState>();
    if (hs) {
        hs->shown.reset();
        hs->game_elapsed = 0.f;
        hs->prev_death_count = 0;
        hs->prev_phase = GameClock::Phase::Day;
        hs->prev_slots_per_type = 0;
        hs->bathroom_warned = false;
        hs->food_warned = false;
        hs->medtent_warned = false;
        hs->bathroom_overload_timer = 0.f;
        hs->food_overload_timer = 0.f;
        hs->medtent_overload_timer = 0.f;
    }

    // Reset difficulty state
    auto* diff = EntityHelper::get_singleton_cmp<DifficultyState>();
    if (diff) {
        diff->day_number = 1;
        diff->spawn_rate_mult = 1.0f;
        diff->crowd_size_mult = 1.0f;
        diff->event_timer = 0.f;
        diff->next_event_time = 120.f;
    }
}

bool should_escape_quit() {
    auto* pds = EntityHelper::get_singleton_cmp<PathDrawState>();
    if (pds && (pds->is_drawing || pds->demolish_mode)) {
        return false;
    }
    return true;
}
