#include "entity_makers.h"

#include "afterhours/src/core/entity_query.h"
#include "afterhours/src/plugins/input_system.h"
#include "afterhours/src/plugins/window_manager.h"
#include "engine/random_engine.h"
#include "game.h"
#include "input_mapping.h"
#include "render_helpers.h"
#include "systems.h"
#include "update_helpers.h"

using namespace afterhours;

Entity& make_sophie() {
    Entity& sophie = EntityHelper::createPermanentEntity();

    sophie.addComponent<ProvidesCamera>();
    EntityHelper::registerSingleton<ProvidesCamera>(sophie);

    // Center camera on the initial bought park (fence at 0/51), not on
    // the whole preallocated grid — most of it is Locked land.
    sophie.get<ProvidesCamera>().cam.target = {(PLAY_MAX + 1) / 2.0f, 0,
                                               (PLAY_MAX + 1) / 2.0f};
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

    sophie.addComponent<NuxManager>();
    EntityHelper::registerSingleton<NuxManager>(sophie);

    sophie.addComponent<VisibleRegion>();
    EntityHelper::registerSingleton<VisibleRegion>(sophie);

    sophie.addComponent<Economy>();
    EntityHelper::registerSingleton<Economy>(sophie);

    sophie.addComponent<InspectionState>();
    EntityHelper::registerSingleton<InspectionState>(sophie);

    sophie.addComponent<StaffState>();
    EntityHelper::registerSingleton<StaffState>(sophie);

    sophie.addComponent<LineupState>();
    EntityHelper::registerSingleton<LineupState>(sophie);

    sophie.addComponent<EventDirectorState>();
    EntityHelper::registerSingleton<EventDirectorState>(sophie);

    sophie.addComponent<ParkRatingState>();
    EntityHelper::registerSingleton<ParkRatingState>(sophie);

    sophie.addComponent<StageQueueState>();
    EntityHelper::registerSingleton<StageQueueState>(sophie);

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

    // Ticket income: an agent entering through a gate tile pays once.
    // SpawnAgentSystem spawns at the gate, and E2E spawns at a gate do the
    // same; agents spawned elsewhere (e.g. mid-map crush tests) pay nothing.
    if (grid && grid->in_bounds(grid_x, grid_z) &&
        grid->at(grid_x, grid_z).type == TileType::Gate) {
        if (auto* econ = EntityHelper::get_singleton_cmp<Economy>()) {
            econ->earn(econ->ticket_price);
            // Ticket sale: +$N floater at the gate (throttled/merged
            // by spawn_money_floater).
            spawn_money_floater(grid_x * TILESIZE, grid_z * TILESIZE,
                                econ->ticket_price);
        }
        // Paid arrivals count toward attendee milestones.
        if (auto* gs = EntityHelper::get_singleton_cmp<GameState>()) {
            gs->total_arrivals++;
        }
    }

    auto& rng = RandomEngine::get();

    // Random color variety
    e.get<Agent>().color_idx = static_cast<uint8_t>(rng.get_int(0, 7));
    e.addComponent<AgentNeeds>();
    auto& needs = e.get<AgentNeeds>();
    needs.bathroom_threshold = rng.get_float(30.f, 90.f);
    needs.food_threshold = rng.get_float(45.f, 120.f);
    // Heat/bar thresholds vary per agent but must NOT consume the shared
    // RandomEngine stream: movement, pathfinding, and stage-spot
    // decisions all draw from it, so two extra draws per spawn would
    // shift every later draw (and every timing-marginal test downstream).
    // A splitmix hash of the entity id gives a stable per-agent spread.
    auto hash01 = [](uint64_t x) {
        x += 0x9e3779b97f4a7c15ULL;
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
        return (x ^ (x >> 31)) & 0xffffffffULL;
    };
    uint64_t h = hash01(static_cast<uint64_t>(e.id));
    needs.heat_threshold = 90.f + (h % 9000) / 100.f;          // 90-180 sec
    needs.bar_threshold = 60.f + ((h >> 16) % 12000) / 100.f;  // 60-180 sec

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
    // Money floaters are plain data (see components.h), not entities.
    money_floaters().items.clear();
    auto staff = EntityQuery().whereHasComponent<StaffMember>().gen();
    for (Entity& s : staff) s.cleanup = true;
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
        gs->injury_count = 0;
        gs->total_arrivals = 0;
        gs->milestone_idx = 0;
        gs->total_agents_served = 0;
        gs->time_survived = 0.f;
        gs->max_attendees = 0;
        gs->show_data_layer = false;
        gs->show_debug = false;
        gs->speed_multiplier = 1.0f;
        gs->agents_exited = 0;
        gs->carryover_count = 0;
    }

    // Reset inspection state
    auto* insp = EntityHelper::get_singleton_cmp<InspectionState>();
    if (insp) {
        insp->last_grade = '-';
        insp->last_score = -1;
        insp->inspections_done = 0;
        insp->consecutive_bad = 0;
        insp->density_violations = 0;
        insp->deaths_at_last = 0;
        insp->injuries_at_last = 0;
        insp->last_inspected_day = 1;
        insp->shutdown_triggered = false;
        insp->force_inspection = false;
    }

    // Reset spawn state
    auto* ss = EntityHelper::get_singleton_cmp<SpawnState>();
    if (ss) {
        ss->interval = DEFAULT_SPAWN_INTERVAL;
        ss->timer = 0.f;
        ss->enabled = true;
        ss->manual_override = false;
    }

    // Reset economy
    auto* econ = EntityHelper::get_singleton_cmp<Economy>();
    if (econ) {
        econ->cash = STARTING_CASH;
        econ->ticket_price = DEFAULT_TICKET_PRICE;
        econ->total_earned = 0;
        econ->total_spent = 0;
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

    // Reset NUX manager
    auto nux_ents = EntityQuery().whereHasComponent<NuxHint>().gen();
    for (Entity& n : nux_ents) n.cleanup = true;
    auto* nm = EntityHelper::get_singleton_cmp<NuxManager>();
    if (nm) {
        nm->initialized = false;
        nm->bathroom_warned = false;
        nm->food_warned = false;
        nm->medtent_warned = false;
        nm->bathroom_overload_timer = 0.f;
        nm->food_overload_timer = 0.f;
        nm->medtent_overload_timer = 0.f;
    }

    // Reset staff payroll bookkeeping (staff entities cleared above)
    auto* staff_state = EntityHelper::get_singleton_cmp<StaffState>();
    if (staff_state) {
        staff_state->last_payroll_day = 1;
        staff_state->quit_count = 0;
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

    // Reset lineup: tier, bookings, and today's acts all clear. The tier
    // is re-derived from total_arrivals (reset above to 0) on the next
    // frame, so no stale unlocks survive a restart.
    auto* lineup = EntityHelper::get_singleton_cmp<LineupState>();
    if (lineup) {
        lineup->tier = 0;
        lineup->last_rollover_day = 1;
        lineup->booked.clear();
        lineup->active.clear();
    }

    // Reset the view to the default framing: iso ON, default zoom
    // (distance), camera re-centred on the park centre. Rotation is
    // intentionally left alone (existing behavior). Without the
    // distance/target reset, a previous run's zoomed-out camera would
    // leak into the fresh game (e.g. 17_iso_default captured at the
    // pinch test's distance 50 instead of the default 18).
    if (auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>()) {
        cam->cam.iso_mode = true;
        cam->cam.target = {(PLAY_MAX + 1) / 2.0f, 0, (PLAY_MAX + 1) / 2.0f};
        cam->cam.set_distance(IsometricCamera::DEFAULT_DISTANCE);
    }

    // Reset the park window to its default tab (Park).
    park_window_active_tab() = 0;

    // Reset the event director: default seed + fresh day bookkeeping,
    // so a restart never inherits the previous run's event sequence.
    reset_event_director();

    // Reset the stage queue: default capacity, empty line.
    if (auto* qs = EntityHelper::get_singleton_cmp<StageQueueState>()) {
        qs->watch_capacity = STAGE_WATCH_CAPACITY;
        qs->watching_cached = 0;
        qs->queue_length_cached = 0;
        qs->queue_wait_total = 0.f;
    }

    // Reset the park rating: 500, no harm, no pressures, yesterday 500.
    if (auto* rating = EntityHelper::get_singleton_cmp<ParkRatingState>()) {
        rating->rating = PARK_RATING_START;
        rating->target = PARK_RATING_START;
        rating->harm_pool = 0.f;
        rating->need_pressure = 0.f;
        rating->crowding_pressure = 0.f;
        rating->yesterday_rating = PARK_RATING_START;
        rating->day_start_rating = PARK_RATING_START;
        rating->last_day = 1;
        rating->last_death_count = 0;
        rating->last_injury_count = 0;
    }
}

bool should_escape_quit() {
    auto* pds = EntityHelper::get_singleton_cmp<PathDrawState>();
    if (pds && (pds->is_drawing || pds->demolish_mode)) {
        return false;
    }
    return true;
}
