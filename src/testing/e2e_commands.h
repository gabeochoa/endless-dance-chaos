#pragma once

// Custom E2E command handlers for game-specific test commands

#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "afterhours/src/core/entity_query.h"
#include "afterhours/src/plugins/e2e_testing/e2e_testing.h"
#include "components.h"
#include "entity_makers.h"
#include "game.h"
#include "systems.h"

using namespace afterhours;

// Helper: compare with operator string (int)
inline bool compare_op(int actual, const std::string& op, int expected) {
    if (op == "eq" || op == "==") return actual == expected;
    if (op == "gt" || op == ">") return actual > expected;
    if (op == "lt" || op == "<") return actual < expected;
    if (op == "gte" || op == ">=") return actual >= expected;
    if (op == "lte" || op == "<=") return actual <= expected;
    if (op == "ne" || op == "!=") return actual != expected;
    return false;
}

// Helper: compare with operator string (float)
inline bool compare_op_f(float actual, const std::string& op, float expected) {
    if (op == "eq" || op == "==") return std::abs(actual - expected) < 0.001f;
    if (op == "gt" || op == ">") return actual > expected;
    if (op == "lt" || op == "<") return actual < expected;
    if (op == "gte" || op == ">=") return actual >= expected;
    if (op == "lte" || op == "<=") return actual <= expected;
    if (op == "ne" || op == "!=") return std::abs(actual - expected) >= 0.001f;
    return false;
}

// Helper: parse TileType from string
inline TileType parse_tile_type(const std::string& s) {
    std::string lower = s;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower == "grass") return TileType::Grass;
    if (lower == "path") return TileType::Path;
    if (lower == "fence") return TileType::Fence;
    if (lower == "gate") return TileType::Gate;
    if (lower == "stage") return TileType::Stage;
    if (lower == "stagefloor" || lower == "stage_floor")
        return TileType::StageFloor;
    if (lower == "bathroom") return TileType::Bathroom;
    if (lower == "food") return TileType::Food;
    return TileType::Grass;
}

// Helper: parse FacilityType from string
inline FacilityType parse_facility_type(const std::string& s) {
    std::string lower = s;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower == "bathroom") return FacilityType::Bathroom;
    if (lower == "food") return FacilityType::Food;
    if (lower == "stage") return FacilityType::Stage;
    return FacilityType::Bathroom;
}

// spawn_agent X Z TYPE [TARGET_X TARGET_Z]
struct HandleSpawnAgentCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("spawn_agent")) return;
        if (!cmd.has_args(3)) {
            cmd.fail("spawn_agent requires X Z TYPE arguments");
            return;
        }
        int x = cmd.arg_as<int>(0);
        int z = cmd.arg_as<int>(1);
        FacilityType type = parse_facility_type(cmd.arg(2));
        int tx =
            cmd.has_args(5) ? cmd.arg_as<int>(3) : (STAGE_X + STAGE_SIZE / 2);
        int tz =
            cmd.has_args(5) ? cmd.arg_as<int>(4) : (STAGE_Z + STAGE_SIZE / 2);
        make_agent(x, z, type, tx, tz);
        EntityHelper::merge_entity_arrays();
        cmd.consume();
    }
};

// spawn_agents X Z COUNT TYPE
struct HandleSpawnAgentsCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("spawn_agents")) return;
        if (!cmd.has_args(4)) {
            cmd.fail("spawn_agents requires X Z COUNT TYPE arguments");
            return;
        }
        int x = cmd.arg_as<int>(0);
        int z = cmd.arg_as<int>(1);
        int count = cmd.arg_as<int>(2);
        FacilityType type = parse_facility_type(cmd.arg(3));
        int tx = STAGE_X + STAGE_SIZE / 2;
        int tz = STAGE_Z + STAGE_SIZE / 2;
        for (int i = 0; i < count; i++) {
            make_agent(x, z, type, tx, tz);
        }
        EntityHelper::merge_entity_arrays();
        cmd.consume();
    }
};

// clear_agents
struct HandleClearAgentsCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("clear_agents")) return;
        auto agents = EntityQuery().whereHasComponent<Agent>().gen();
        for (Entity& agent : agents) {
            agent.cleanup = true;
        }
        EntityHelper::cleanup();
        cmd.consume();
    }
};

// clear_map
struct HandleClearMapCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("clear_map")) return;
        // Clear agents
        auto agents = EntityQuery().whereHasComponent<Agent>().gen();
        for (Entity& agent : agents) {
            agent.cleanup = true;
        }
        EntityHelper::cleanup();

        // Reset grid to all grass
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (grid) {
            for (auto& tile : grid->tiles) {
                tile.type = TileType::Grass;
                tile.agent_count = 0;
            }
        }
        cmd.consume();
    }
};

// reset_game
struct HandleResetGameCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("reset_game")) return;

        // Clear all non-permanent entities
        auto agents = EntityQuery().whereHasComponent<Agent>().gen();
        for (Entity& agent : agents) {
            agent.cleanup = true;
        }
        EntityHelper::cleanup();

        // Reset grid: clear everything and re-init perimeter + facilities
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (grid) {
            for (auto& tile : grid->tiles) {
                tile.type = TileType::Grass;
                tile.agent_count = 0;
            }
            grid->init_perimeter();
        }

        // Reset game state
        auto* state = EntityHelper::get_singleton_cmp<GameState>();
        if (state) {
            state->status = GameStatus::Running;
            state->game_time = 0.f;
            state->show_data_layer = false;
            state->death_count = 0;
            state->speed_multiplier = 1.0f;
            state->total_agents_served = 0;
            state->time_survived = 0.f;
            state->max_attendees = 0;
        }

        // Reset spawn state
        auto* ss = EntityHelper::get_singleton_cmp<SpawnState>();
        if (ss) {
            ss->interval = DEFAULT_SPAWN_INTERVAL;
            ss->timer = 0.f;
            ss->enabled = true;
        }

        // Reset game clock
        auto* clock = EntityHelper::get_singleton_cmp<GameClock>();
        if (clock) {
            clock->game_time_minutes = 600.0f;  // 10:00am
            clock->speed = GameSpeed::OneX;
        }

        // Reset artist schedule
        auto* sched = EntityHelper::get_singleton_cmp<ArtistSchedule>();
        if (sched) {
            sched->schedule.clear();
            sched->stage_state = StageState::Idle;
            sched->current_artist_idx = -1;
        }

        // Reset additional game state fields
        if (state) {
            state->agents_exited = 0;
            state->carryover_count = 0;
            state->show_debug = false;
        }

        // Clear pheromones
        if (grid) {
            for (auto& tile : grid->tiles) {
                tile.pheromone = {0, 0, 0, 0, 0};
            }
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

        // Clear particles, toasts, and active events
        auto particles = EntityQuery().whereHasComponent<Particle>().gen();
        for (Entity& p : particles) p.cleanup = true;
        auto toasts = EntityQuery().whereHasComponent<ToastMessage>().gen();
        for (Entity& t : toasts) t.cleanup = true;
        auto events = EntityQuery().whereHasComponent<ActiveEvent>().gen();
        for (Entity& ev : events) ev.cleanup = true;
        EntityHelper::cleanup();

        cmd.consume();
    }
};

// place_facility TYPE X Z
struct HandlePlaceFacilityCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("place_facility")) return;
        if (!cmd.has_args(3)) {
            cmd.fail("place_facility requires TYPE X Z arguments");
            return;
        }
        TileType type = parse_tile_type(cmd.arg(0));
        int x = cmd.arg_as<int>(1);
        int z = cmd.arg_as<int>(2);

        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid || !grid->in_bounds(x, z)) {
            cmd.fail("place_facility: position out of bounds");
            return;
        }
        grid->at(x, z).type = type;
        cmd.consume();
    }
};

// set_tile X Z TYPE
struct HandleSetTileCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("set_tile")) return;
        if (!cmd.has_args(3)) {
            cmd.fail("set_tile requires X Z TYPE arguments");
            return;
        }
        int x = cmd.arg_as<int>(0);
        int z = cmd.arg_as<int>(1);
        TileType type = parse_tile_type(cmd.arg(2));

        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid || !grid->in_bounds(x, z)) {
            cmd.fail("set_tile: position out of bounds");
            return;
        }
        grid->at(x, z).type = type;
        cmd.consume();
    }
};

// get_agent_count
struct HandleGetAgentCountCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("get_agent_count")) return;
        int count = (int) EntityQuery().whereHasComponent<Agent>().gen_count();
        log_info("[E2E] Agent count: {}", count);
        cmd.consume();
    }
};

// get_density X Z
struct HandleGetDensityCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("get_density")) return;
        if (!cmd.has_args(2)) {
            cmd.fail("get_density requires X Z arguments");
            return;
        }
        int x = cmd.arg_as<int>(0);
        int z = cmd.arg_as<int>(1);

        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid || !grid->in_bounds(x, z)) {
            cmd.fail("get_density: position out of bounds");
            return;
        }
        log_info("[E2E] Density at ({}, {}): {}", x, z,
                 grid->at(x, z).agent_count);
        cmd.consume();
    }
};

// assert_agent_count OP VALUE
struct HandleAssertAgentCountCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("assert_agent_count")) return;
        if (!cmd.has_args(2)) {
            cmd.fail("assert_agent_count requires OP VALUE arguments");
            return;
        }
        std::string op = cmd.arg(0);
        int expected = cmd.arg_as<int>(1);
        int actual = (int) EntityQuery().whereHasComponent<Agent>().gen_count();

        if (!compare_op(actual, op, expected)) {
            cmd.fail(
                fmt::format("assert_agent_count failed: {} {} {} (actual: {})",
                            actual, op, expected, actual));
        } else {
            cmd.consume();
        }
    }
};

// assert_density X Z OP VALUE
struct HandleAssertDensityCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("assert_density")) return;
        if (!cmd.has_args(4)) {
            cmd.fail("assert_density requires X Z OP VALUE arguments");
            return;
        }
        int x = cmd.arg_as<int>(0);
        int z = cmd.arg_as<int>(1);
        std::string op = cmd.arg(2);
        int expected = cmd.arg_as<int>(3);

        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid || !grid->in_bounds(x, z)) {
            cmd.fail("assert_density: position out of bounds");
            return;
        }
        int actual = grid->at(x, z).agent_count;

        if (!compare_op(actual, op, expected)) {
            cmd.fail(fmt::format(
                "assert_density at ({},{}) failed: {} {} {} (actual: {})", x, z,
                actual, op, expected, actual));
        } else {
            cmd.consume();
        }
    }
};

// assert_tile_type X Z TYPE
struct HandleAssertTileTypeCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("assert_tile_type")) return;
        if (!cmd.has_args(3)) {
            cmd.fail("assert_tile_type requires X Z TYPE arguments");
            return;
        }
        int x = cmd.arg_as<int>(0);
        int z = cmd.arg_as<int>(1);
        TileType expected = parse_tile_type(cmd.arg(2));

        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid || !grid->in_bounds(x, z)) {
            cmd.fail("assert_tile_type: position out of bounds");
            return;
        }
        TileType actual = grid->at(x, z).type;

        if (actual != expected) {
            cmd.fail(fmt::format(
                "assert_tile_type at ({},{}) failed: expected {}, got {}", x, z,
                cmd.arg(2), static_cast<int>(actual)));
        } else {
            cmd.consume();
        }
    }
};

// Helper: convert grid coordinates to screen coordinates
inline std::optional<raylib::Vector2> grid_to_screen(int gx, int gz) {
    auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
    if (!cam) return std::nullopt;
    raylib::Vector3 world_pos = {gx * TILESIZE, 0.0f, gz * TILESIZE};
    raylib::Vector2 screen_pos =
        raylib::GetWorldToScreen(world_pos, cam->cam.camera);
    return screen_pos;
}

// move_to_grid X Z - move mouse cursor to screen position of grid cell
struct HandleMoveToGridCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("move_to_grid")) return;
        if (!cmd.has_args(2)) {
            cmd.fail("move_to_grid requires X Z arguments");
            return;
        }
        int gx = cmd.arg_as<int>(0);
        int gz = cmd.arg_as<int>(1);

        auto screen = grid_to_screen(gx, gz);
        if (!screen) {
            cmd.fail("move_to_grid: no camera available");
            return;
        }
        afterhours::testing::test_input::set_mouse_position(screen->x,
                                                            screen->y);
        cmd.consume();
    }
};

// click_grid X Z - simulate a left-click at a grid cell via input injection.
// Converts grid coords to screen position, injects the mouse position and
// a press event via afterhours test_input. Pre-sets hover so PathBuildSystem
// can act on the next frame without waiting for the render-phase
// HoverTrackingSystem.
struct HandleClickGridCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("click_grid")) return;
        if (!cmd.has_args(2)) {
            cmd.fail("click_grid requires X Z arguments");
            return;
        }
        int gx = cmd.arg_as<int>(0);
        int gz = cmd.arg_as<int>(1);

        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid || !grid->in_bounds(gx, gz)) {
            cmd.fail("click_grid: position out of bounds");
            return;
        }

        auto* pds = EntityHelper::get_singleton_cmp<PathDrawState>();
        if (!pds) {
            cmd.fail("click_grid: no PathDrawState");
            return;
        }

        // Convert grid to screen coords and inject via afterhours test_input
        auto screen = grid_to_screen(gx, gz);
        if (screen) {
            testing::test_input::simulate_click(screen->x, screen->y);
        } else {
            // Fallback: just inject press without position
            testing::test_input::simulate_mouse_press();
        }

        // Pre-set hover so PathBuildSystem has valid grid coords next frame
        pds->hover_x = gx;
        pds->hover_z = gz;
        pds->hover_valid = true;

        log_info("[E2E] click_grid ({}, {}) injected mouse press", gx, gz);
        cmd.consume();
    }
};

// assert_agent_near X Z RADIUS - assert at least one agent within RADIUS tiles
struct HandleAssertAgentNearCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("assert_agent_near")) return;
        if (!cmd.has_args(3)) {
            cmd.fail("assert_agent_near requires X Z RADIUS arguments");
            return;
        }
        int gx = cmd.arg_as<int>(0);
        int gz = cmd.arg_as<int>(1);
        float radius = cmd.arg_as<float>(2);

        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) {
            cmd.fail("assert_agent_near: no grid");
            return;
        }

        ::vec2 target_world = grid->grid_to_world(gx, gz);
        float radius_world = radius * TILESIZE;

        auto agents = EntityQuery()
                          .whereHasComponent<Agent>()
                          .whereHasComponent<Transform>()
                          .gen();
        for (Entity& agent : agents) {
            auto& tf = agent.get<Transform>();
            float dx = tf.position.x - target_world.x;
            float dz = tf.position.y - target_world.y;
            float dist = std::sqrt(dx * dx + dz * dz);
            if (dist <= radius_world) {
                cmd.consume();
                return;
            }
        }

        // Count for error message
        int count = (int) EntityQuery().whereHasComponent<Agent>().gen_count();
        cmd.fail(
            fmt::format("assert_agent_near ({},{}) r={}: no agent found nearby "
                        "({} total agents)",
                        gx, gz, radius, count));
    }
};

// set_spawn_rate INTERVAL - set seconds between agent spawns
struct HandleSetSpawnRateCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("set_spawn_rate")) return;
        if (!cmd.has_args(1)) {
            cmd.fail("set_spawn_rate requires INTERVAL argument");
            return;
        }
        float interval = cmd.arg_as<float>(0);
        auto* ss = EntityHelper::get_singleton_cmp<SpawnState>();
        if (!ss) {
            cmd.fail("set_spawn_rate: no SpawnState");
            return;
        }
        ss->interval = interval;
        cmd.consume();
    }
};

// set_spawn_enabled 0|1 - enable/disable auto-spawning
struct HandleSetSpawnEnabledCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("set_spawn_enabled")) return;
        if (!cmd.has_args(1)) {
            cmd.fail("set_spawn_enabled requires 0 or 1 argument");
            return;
        }
        int val = cmd.arg_as<int>(0);
        auto* ss = EntityHelper::get_singleton_cmp<SpawnState>();
        if (!ss) {
            cmd.fail("set_spawn_enabled: no SpawnState");
            return;
        }
        ss->enabled = (val != 0);
        cmd.consume();
    }
};

// force_need TYPE - force ALL agents to need bathroom or food immediately
struct HandleForceNeedCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("force_need")) return;
        if (!cmd.has_args(1)) {
            cmd.fail("force_need requires TYPE (bathroom|food) argument");
            return;
        }
        std::string type_str = cmd.arg(0);
        std::transform(type_str.begin(), type_str.end(), type_str.begin(),
                       ::tolower);

        auto agents = EntityQuery()
                          .whereHasComponent<Agent>()
                          .whereHasComponent<AgentNeeds>()
                          .gen();
        for (Entity& agent_e : agents) {
            auto& needs = agent_e.get<AgentNeeds>();
            if (type_str == "bathroom") {
                needs.needs_bathroom = true;
            } else if (type_str == "food") {
                needs.needs_food = true;
            }
            // Interrupt watching so agents act on the new need
            if (!agent_e.is_missing<WatchingStage>()) {
                agent_e.removeComponent<WatchingStage>();
            }
        }
        cmd.consume();
    }
};

// assert_agents_at_facility TYPE OP COUNT
struct HandleAssertAgentsAtFacilityCommand
    : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("assert_agents_at_facility")) return;
        if (!cmd.has_args(3)) {
            cmd.fail(
                "assert_agents_at_facility requires TYPE OP COUNT arguments");
            return;
        }
        std::string type_str = cmd.arg(0);
        std::string op = cmd.arg(1);
        int expected = cmd.arg_as<int>(2);

        FacilityType ftype = parse_facility_type(type_str);

        int count = 0;
        auto agents = EntityQuery()
                          .whereHasComponent<Agent>()
                          .whereHasComponent<BeingServiced>()
                          .gen();
        for (Entity& agent : agents) {
            if (agent.get<BeingServiced>().facility_type == ftype) {
                count++;
            }
        }

        if (!compare_op(count, op, expected)) {
            cmd.fail(fmt::format(
                "assert_agents_at_facility {} failed: {} {} {} (actual: {})",
                type_str, count, op, expected, count));
        } else {
            cmd.consume();
        }
    }
};

// assert_agent_watching OP COUNT
struct HandleAssertAgentWatchingCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("assert_agent_watching")) return;
        if (!cmd.has_args(2)) {
            cmd.fail("assert_agent_watching requires OP COUNT arguments");
            return;
        }
        std::string op = cmd.arg(0);
        int expected = cmd.arg_as<int>(1);

        int count =
            (int) EntityQuery().whereHasComponent<WatchingStage>().gen_count();

        if (!compare_op(count, op, expected)) {
            cmd.fail(fmt::format(
                "assert_agent_watching failed: {} {} {} (actual: {})", count,
                op, expected, count));
        } else {
            cmd.consume();
        }
    }
};

// place_gate X Z - place a 2x1 gate (vertical) at position
struct HandlePlaceGateCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("place_gate")) return;
        if (!cmd.has_args(2)) {
            cmd.fail("place_gate requires X Z arguments");
            return;
        }
        int x = cmd.arg_as<int>(0);
        int z = cmd.arg_as<int>(1);
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) {
            cmd.fail("place_gate: no grid");
            return;
        }
        if (grid->in_bounds(x, z)) grid->at(x, z).type = TileType::Gate;
        if (grid->in_bounds(x, z + 1)) grid->at(x, z + 1).type = TileType::Gate;
        cmd.consume();
    }
};

// assert_gate_count OP VALUE
struct HandleAssertGateCountCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("assert_gate_count")) return;
        if (!cmd.has_args(2)) {
            cmd.fail("assert_gate_count requires OP VALUE arguments");
            return;
        }
        std::string op = cmd.arg(0);
        int expected = cmd.arg_as<int>(1);
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) {
            cmd.fail("assert_gate_count: no grid");
            return;
        }

        int count = 0;
        for (const auto& tile : grid->tiles) {
            if (tile.type == TileType::Gate) count++;
        }
        if (!compare_op(count, op, expected)) {
            cmd.fail(
                fmt::format("assert_gate_count failed: {} {} {} (actual: {})",
                            count, op, expected, count));
        } else {
            cmd.consume();
        }
    }
};

// assert_blocked X Z - assert tile is fence (not walkable)
struct HandleAssertBlockedCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("assert_blocked")) return;
        if (!cmd.has_args(2)) {
            cmd.fail("assert_blocked requires X Z arguments");
            return;
        }
        int x = cmd.arg_as<int>(0);
        int z = cmd.arg_as<int>(1);
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid || !grid->in_bounds(x, z)) {
            cmd.fail("assert_blocked: out of bounds");
            return;
        }
        if (grid->at(x, z).type != TileType::Fence) {
            cmd.fail(fmt::format(
                "assert_blocked ({},{}) failed: tile is not fence (type={})", x,
                z, static_cast<int>(grid->at(x, z).type)));
        } else {
            cmd.consume();
        }
    }
};

// toggle_debug - toggle debug panel
struct HandleToggleDebugCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("toggle_debug")) return;
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        if (!gs) {
            cmd.fail("toggle_debug: no GameState");
            return;
        }
        gs->show_debug = !gs->show_debug;
        log_info("[E2E] Debug panel: {}", gs->show_debug ? "ON" : "OFF");
        cmd.consume();
    }
};

// trigger_game_over - force game over state
struct HandleTriggerGameOverCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("trigger_game_over")) return;
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        if (!gs) {
            cmd.fail("trigger_game_over: no GameState");
            return;
        }
        gs->death_count = gs->max_deaths;
        gs->status = GameStatus::GameOver;
        log_info("[E2E] Game over triggered");
        cmd.consume();
    }
};

// assert_game_status STATUS - assert running/gameover
struct HandleAssertGameStatusCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("assert_game_status")) return;
        if (!cmd.has_args(1)) {
            cmd.fail("assert_game_status requires STATUS argument");
            return;
        }
        std::string expected = cmd.arg(0);
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        if (!gs) {
            cmd.fail("assert_game_status: no GameState");
            return;
        }

        bool want_gameover =
            (expected == "gameover" || expected == "game_over");
        bool is_gameover = gs->is_game_over();

        if (want_gameover != is_gameover) {
            cmd.fail(
                fmt::format("assert_game_status failed: expected {}, got {}",
                            expected, is_gameover ? "gameover" : "running"));
        } else {
            cmd.consume();
        }
    }
};

// toggle_overlay - toggle density overlay on/off
struct HandleToggleOverlayCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("toggle_overlay")) return;
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        if (!gs) {
            cmd.fail("toggle_overlay: no GameState");
            return;
        }
        gs->show_data_layer = !gs->show_data_layer;
        log_info("[E2E] Overlay toggled: {}",
                 gs->show_data_layer ? "ON" : "OFF");
        cmd.consume();
    }
};

// assert_overlay on|off - assert overlay state
struct HandleAssertOverlayCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("assert_overlay")) return;
        if (!cmd.has_args(1)) {
            cmd.fail("assert_overlay requires on/off argument");
            return;
        }
        std::string expected = cmd.arg(0);
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        if (!gs) {
            cmd.fail("assert_overlay: no GameState");
            return;
        }
        bool want_on = (expected == "on" || expected == "1");
        if (gs->show_data_layer != want_on) {
            cmd.fail(fmt::format("assert_overlay failed: expected {}, got {}",
                                 expected, gs->show_data_layer ? "on" : "off"));
        } else {
            cmd.consume();
        }
    }
};

// set_agent_speed MULTIPLIER - scale all agent movement speeds
struct HandleSetAgentSpeedCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("set_agent_speed")) return;
        if (!cmd.has_args(1)) {
            cmd.fail("set_agent_speed requires MULTIPLIER argument");
            return;
        }
        float mult = cmd.arg_as<float>(0);
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        if (!gs) {
            cmd.fail("set_agent_speed: no GameState");
            return;
        }
        gs->speed_multiplier = mult;
        log_info("[E2E] Agent speed multiplier set to {}", mult);
        cmd.consume();
    }
};

// get_death_count - log current death count
struct HandleGetDeathCountCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("get_death_count")) return;
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        int count = gs ? gs->death_count : 0;
        log_info("[E2E] Death count: {}/{}", count, gs ? gs->max_deaths : 0);
        cmd.consume();
    }
};

// assert_death_count OP VALUE
struct HandleAssertDeathCountCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("assert_death_count")) return;
        if (!cmd.has_args(2)) {
            cmd.fail("assert_death_count requires OP VALUE arguments");
            return;
        }
        std::string op = cmd.arg(0);
        int expected = cmd.arg_as<int>(1);
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        int actual = gs ? gs->death_count : 0;

        if (!compare_op(actual, op, expected)) {
            cmd.fail(
                fmt::format("assert_death_count failed: {} {} {} (actual: {})",
                            actual, op, expected, actual));
        } else {
            cmd.consume();
        }
    }
};

// assert_agent_hp X Z OP VALUE - assert HP of agents at tile
struct HandleAssertAgentHpCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("assert_agent_hp")) return;
        if (!cmd.has_args(4)) {
            cmd.fail("assert_agent_hp requires X Z OP VALUE arguments");
            return;
        }
        int tx = cmd.arg_as<int>(0);
        int tz = cmd.arg_as<int>(1);
        std::string op = cmd.arg(2);
        float expected = cmd.arg_as<float>(3);

        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) {
            cmd.fail("assert_agent_hp: no grid");
            return;
        }

        auto agents = EntityQuery()
                          .whereHasComponent<Agent>()
                          .whereHasComponent<Transform>()
                          .whereHasComponent<AgentHealth>()
                          .gen();
        bool found_any = false;
        for (Entity& agent : agents) {
            auto& tf = agent.get<Transform>();
            auto [gx, gz] = grid->world_to_grid(tf.position.x, tf.position.y);
            if (gx == tx && gz == tz) {
                found_any = true;
                float actual = agent.get<AgentHealth>().hp;
                if (!compare_op_f(actual, op, expected)) {
                    cmd.fail(fmt::format(
                        "assert_agent_hp at ({},{}) failed: {} {} {} "
                        "(actual: {:.3f})",
                        tx, tz, actual, op, expected, actual));
                    return;
                }
            }
        }
        if (!found_any) {
            cmd.fail(
                fmt::format("assert_agent_hp: no agents at ({},{})", tx, tz));
            return;
        }
        cmd.consume();
    }
};

// draw_path_rect X1 Z1 X2 Z2
struct HandleDrawPathRectCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("draw_path_rect")) return;
        if (!cmd.has_args(4)) {
            cmd.fail("draw_path_rect requires X1 Z1 X2 Z2 arguments");
            return;
        }
        int x1 = cmd.arg_as<int>(0);
        int z1 = cmd.arg_as<int>(1);
        int x2 = cmd.arg_as<int>(2);
        int z2 = cmd.arg_as<int>(3);

        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) {
            cmd.fail("draw_path_rect: no grid found");
            return;
        }

        int min_x = std::min(x1, x2);
        int min_z = std::min(z1, z2);
        int max_x = std::max(x1, x2);
        int max_z = std::max(z1, z2);

        for (int z = min_z; z <= max_z; z++) {
            for (int x = min_x; x <= max_x; x++) {
                if (!grid->in_bounds(x, z)) continue;
                if (grid->at(x, z).type != TileType::Grass) continue;
                grid->at(x, z).type = TileType::Path;
            }
        }
        cmd.consume();
    }
};

// set_time HOUR MINUTE — set game clock to specific time
struct HandleSetTimeCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("set_time")) return;
        if (cmd.args.size() < 2) {
            log_warn("set_time requires HOUR MINUTE");
            cmd.consume();
            return;
        }
        int hour = std::stoi(cmd.args[0]);
        int minute = std::stoi(cmd.args[1]);
        auto* clock = EntityHelper::get_singleton_cmp<GameClock>();
        if (clock) {
            clock->game_time_minutes = static_cast<float>(hour * 60 + minute);
            log_info("set_time: {:02d}:{:02d} ({} minutes)", hour, minute,
                     clock->game_time_minutes);
        }
        cmd.consume();
    }
};

// set_speed SPEED — set game speed (paused/1x/2x/4x)
struct HandleSetSpeedCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("set_speed")) return;
        if (cmd.args.empty()) {
            log_warn("set_speed requires SPEED arg");
            cmd.consume();
            return;
        }
        std::string s = cmd.args[0];
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        auto* clock = EntityHelper::get_singleton_cmp<GameClock>();
        if (clock) {
            if (s == "paused" || s == "0")
                clock->speed = GameSpeed::Paused;
            else if (s == "1x" || s == "1")
                clock->speed = GameSpeed::OneX;
            else if (s == "2x" || s == "2")
                clock->speed = GameSpeed::TwoX;
            else if (s == "4x" || s == "4")
                clock->speed = GameSpeed::FourX;
            else
                log_warn("set_speed: unknown speed '{}'", s);
            log_info("set_speed: {}", s);
        }
        cmd.consume();
    }
};

// assert_phase PHASE — assert current phase (day/night/exodus/dead)
struct HandleAssertPhaseCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("assert_phase")) return;
        if (cmd.args.empty()) {
            log_warn("assert_phase requires PHASE arg");
            cmd.consume();
            return;
        }
        auto* clock = EntityHelper::get_singleton_cmp<GameClock>();
        if (!clock) {
            log_warn("assert_phase: no GameClock");
            cmd.consume();
            return;
        }
        std::string expected = cmd.args[0];
        std::transform(expected.begin(), expected.end(), expected.begin(),
                       ::tolower);
        GameClock::Phase phase = clock->get_phase();
        std::string actual_str = GameClock::phase_name(phase);
        std::string actual_lower = actual_str;
        std::transform(actual_lower.begin(), actual_lower.end(),
                       actual_lower.begin(), ::tolower);
        // Handle "dead" matching "dead hours"
        bool match =
            (actual_lower == expected) ||
            (expected == "dead" && phase == GameClock::Phase::DeadHours);
        if (!match) {
            cmd.fail(fmt::format("assert_phase FAILED: expected '{}', got '{}'",
                                 expected, actual_str));
            return;
        }
        log_info("assert_phase PASSED: phase is '{}'", actual_str);
        cmd.consume();
    }
};

// assert_time_between H1 M1 H2 M2 — assert time is in range
struct HandleAssertTimeBetweenCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("assert_time_between")) return;
        if (cmd.args.size() < 4) {
            log_warn("assert_time_between requires H1 M1 H2 M2");
            cmd.consume();
            return;
        }
        auto* clock = EntityHelper::get_singleton_cmp<GameClock>();
        if (!clock) {
            log_warn("assert_time_between: no GameClock");
            cmd.consume();
            return;
        }
        float t1 = static_cast<float>(std::stoi(cmd.args[0]) * 60 +
                                      std::stoi(cmd.args[1]));
        float t2 = static_cast<float>(std::stoi(cmd.args[2]) * 60 +
                                      std::stoi(cmd.args[3]));
        float current = clock->game_time_minutes;
        if (current >= t1 && current <= t2) {
            log_info("assert_time_between PASSED: {} in [{}, {}]", current, t1,
                     t2);
            cmd.consume();
        } else {
            cmd.fail(
                fmt::format("assert_time_between FAILED: {} not in [{}, {}]",
                            current, t1, t2));
        }
    }
};

// assert_stage_state STATE — assert stage state
// (idle/announcing/performing/clearing)
struct HandleAssertStageStateCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("assert_stage_state")) return;
        if (cmd.args.empty()) {
            cmd.consume();
            return;
        }
        auto* sched = EntityHelper::get_singleton_cmp<ArtistSchedule>();
        if (!sched) {
            log_warn("assert_stage_state: no ArtistSchedule");
            cmd.consume();
            return;
        }
        std::string expected = cmd.args[0];
        std::transform(expected.begin(), expected.end(), expected.begin(),
                       ::tolower);
        std::string actual;
        switch (sched->stage_state) {
            case StageState::Idle:
                actual = "idle";
                break;
            case StageState::Announcing:
                actual = "announcing";
                break;
            case StageState::Performing:
                actual = "performing";
                break;
            case StageState::Clearing:
                actual = "clearing";
                break;
        }
        if (actual == expected) {
            log_info("assert_stage_state PASSED: {}", actual);
            cmd.consume();
        } else {
            cmd.fail(fmt::format(
                "assert_stage_state FAILED: expected '{}', got '{}'", expected,
                actual));
        }
    }
};

// force_artist NAME CROWD DURATION [START_H START_M]
struct HandleForceArtistCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("force_artist")) return;
        if (cmd.args.size() < 3) {
            cmd.consume();
            return;
        }
        auto* sched = EntityHelper::get_singleton_cmp<ArtistSchedule>();
        auto* clock = EntityHelper::get_singleton_cmp<GameClock>();
        if (!sched || !clock) {
            cmd.consume();
            return;
        }
        ScheduledArtist a;
        a.name = cmd.args[0];
        a.expected_crowd = std::stoi(cmd.args[1]);
        a.duration_minutes = std::stof(cmd.args[2]);
        if (cmd.args.size() >= 5) {
            int h = std::stoi(cmd.args[3]);
            int m = std::stoi(cmd.args[4]);
            a.start_time_minutes = static_cast<float>(h * 60 + m);
        } else {
            a.start_time_minutes = clock->game_time_minutes + 1.f;
        }
        sched->schedule.insert(sched->schedule.begin(), a);
        log_info("force_artist: '{}' crowd={} dur={}", a.name, a.expected_crowd,
                 a.duration_minutes);
        cmd.consume();
    }
};

// assert_agents_exited OP VALUE
struct HandleAssertAgentsExitedCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("assert_agents_exited")) return;
        if (cmd.args.size() < 2) {
            cmd.consume();
            return;
        }
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        if (!gs) {
            cmd.consume();
            return;
        }
        if (compare_op(gs->agents_exited, cmd.args[0],
                       std::stoi(cmd.args[1]))) {
            log_info("assert_agents_exited PASSED: {} {} {}", gs->agents_exited,
                     cmd.args[0], cmd.args[1]);
            cmd.consume();
        } else {
            cmd.fail(fmt::format(
                "assert_agents_exited FAILED: {} {} {} (actual: {})",
                gs->agents_exited, cmd.args[0], cmd.args[1],
                gs->agents_exited));
        }
    }
};

// assert_carryover_count OP VALUE
struct HandleAssertCarryoverCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("assert_carryover_count")) return;
        if (cmd.args.size() < 2) {
            cmd.consume();
            return;
        }
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        if (!gs) {
            cmd.consume();
            return;
        }
        if (compare_op(gs->carryover_count, cmd.args[0],
                       std::stoi(cmd.args[1]))) {
            log_info("assert_carryover_count PASSED: {}", gs->carryover_count);
            cmd.consume();
        } else {
            cmd.fail(fmt::format("assert_carryover_count FAILED: actual={}",
                                 gs->carryover_count));
        }
    }
};

// set_pheromone X Z CHANNEL VALUE
struct HandleSetPheromoneCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("set_pheromone")) return;
        if (cmd.args.size() < 4) {
            cmd.consume();
            return;
        }
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) {
            cmd.consume();
            return;
        }
        int x = std::stoi(cmd.args[0]), z = std::stoi(cmd.args[1]);
        int ch = std::stoi(cmd.args[2]);
        int val = std::stoi(cmd.args[3]);
        if (grid->in_bounds(x, z) && ch >= 0 && ch < 4) {
            grid->at(x, z).pheromone[ch] =
                static_cast<uint8_t>(std::clamp(val, 0, 255));
        }
        cmd.consume();
    }
};

// assert_pheromone X Z CHANNEL OP VALUE
struct HandleAssertPheromoneCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("assert_pheromone")) return;
        if (cmd.args.size() < 5) {
            cmd.consume();
            return;
        }
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) {
            cmd.consume();
            return;
        }
        int x = std::stoi(cmd.args[0]), z = std::stoi(cmd.args[1]);
        int ch = std::stoi(cmd.args[2]);
        if (grid->in_bounds(x, z) && ch >= 0 && ch < 4) {
            int actual = grid->at(x, z).pheromone[ch];
            if (compare_op(actual, cmd.args[3], std::stoi(cmd.args[4]))) {
                log_info("assert_pheromone PASSED: ({},{}) ch={} val={}", x, z,
                         ch, actual);
                cmd.consume();
            } else {
                cmd.fail(fmt::format(
                    "assert_pheromone FAILED: ({},{}) ch={} actual={} {} {}", x,
                    z, ch, actual, cmd.args[3], cmd.args[4]));
            }
            return;
        }
        cmd.consume();
    }
};

// clear_pheromones — reset all pheromones to 0
struct HandleClearPheromonesCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("clear_pheromones")) return;
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (grid) {
            for (auto& tile : grid->tiles) tile.pheromone = {0, 0, 0, 0, 0};
        }
        cmd.consume();
    }
};

// set_max_attendees VALUE
struct HandleSetMaxAttendeesCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("set_max_attendees")) return;
        if (cmd.args.empty()) {
            cmd.consume();
            return;
        }
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        if (gs) gs->max_attendees = std::stoi(cmd.args[0]);
        cmd.consume();
    }
};

// assert_max_attendees OP VALUE
struct HandleAssertMaxAttendeesCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("assert_max_attendees")) return;
        if (cmd.args.size() < 2) {
            cmd.consume();
            return;
        }
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        if (!gs) {
            cmd.consume();
            return;
        }
        if (compare_op(gs->max_attendees, cmd.args[0],
                       std::stoi(cmd.args[1]))) {
            log_info("assert_max_attendees PASSED: {}", gs->max_attendees);
            cmd.consume();
        } else {
            cmd.fail(fmt::format("assert_max_attendees FAILED: actual={}",
                                 gs->max_attendees));
        }
    }
};

// assert_slots TYPE OP VALUE
struct HandleAssertSlotsCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("assert_slots")) return;
        if (cmd.args.size() < 3) {
            cmd.consume();
            return;
        }
        auto* fs = EntityHelper::get_singleton_cmp<FacilitySlots>();
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        if (!fs || !gs) {
            cmd.consume();
            return;
        }
        int slots = fs->get_slots_per_type(gs->max_attendees);
        if (compare_op(slots, cmd.args[1], std::stoi(cmd.args[2]))) {
            log_info("assert_slots PASSED: {} slots={}", cmd.args[0], slots);
            cmd.consume();
        } else {
            cmd.fail(fmt::format("assert_slots FAILED: {} actual={}",
                                 cmd.args[0], slots));
        }
    }
};

// select_tool TOOL — select a build tool
struct HandleSelectToolCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("select_tool")) return;
        if (cmd.args.empty()) {
            cmd.consume();
            return;
        }
        auto* bs = EntityHelper::get_singleton_cmp<BuilderState>();
        if (!bs) {
            cmd.consume();
            return;
        }
        std::string tool = cmd.args[0];
        std::transform(tool.begin(), tool.end(), tool.begin(), ::tolower);
        if (tool == "path")
            bs->tool = BuildTool::Path;
        else if (tool == "fence")
            bs->tool = BuildTool::Fence;
        else if (tool == "gate")
            bs->tool = BuildTool::Gate;
        else if (tool == "stage")
            bs->tool = BuildTool::Stage;
        else if (tool == "bathroom")
            bs->tool = BuildTool::Bathroom;
        else if (tool == "food")
            bs->tool = BuildTool::Food;
        else if (tool == "demolish")
            bs->tool = BuildTool::Demolish;
        else
            log_warn("select_tool: unknown tool '{}'", tool);
        cmd.consume();
    }
};

// assert_tool TOOL
struct HandleAssertToolCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("assert_tool")) return;
        if (cmd.args.empty()) {
            cmd.consume();
            return;
        }
        auto* bs = EntityHelper::get_singleton_cmp<BuilderState>();
        if (!bs) {
            cmd.consume();
            return;
        }
        std::string expected = cmd.args[0];
        std::transform(expected.begin(), expected.end(), expected.begin(),
                       ::tolower);
        static const char* names[] = {"path",     "fence", "gate",    "stage",
                                      "bathroom", "food",  "demolish"};
        int idx = static_cast<int>(bs->tool);
        std::string actual = (idx >= 0 && idx < 7) ? names[idx] : "unknown";
        if (actual == expected) {
            log_info("assert_tool PASSED: {}", actual);
            cmd.consume();
        } else {
            cmd.fail(fmt::format("assert_tool FAILED: expected '{}', got '{}'",
                                 expected, actual));
        }
    }
};

// place_building TYPE X Z
struct HandlePlaceBuildingCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("place_building")) return;
        if (cmd.args.size() < 3) {
            cmd.consume();
            return;
        }
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) {
            cmd.consume();
            return;
        }
        std::string type = cmd.args[0];
        std::transform(type.begin(), type.end(), type.begin(), ::tolower);
        int x = std::stoi(cmd.args[1]), z = std::stoi(cmd.args[2]);
        if (type == "gate") {
            if (grid->in_bounds(x, z) && grid->in_bounds(x, z + 1)) {
                grid->at(x, z).type = TileType::Gate;
                grid->at(x, z + 1).type = TileType::Gate;
            }
        } else if (type == "stage") {
            for (int dz = 0; dz < 4; dz++)
                for (int dx = 0; dx < 4; dx++)
                    if (grid->in_bounds(x + dx, z + dz))
                        grid->at(x + dx, z + dz).type = TileType::Stage;
        } else if (type == "bathroom") {
            for (int dz = 0; dz < 2; dz++)
                for (int dx = 0; dx < 2; dx++)
                    if (grid->in_bounds(x + dx, z + dz))
                        grid->at(x + dx, z + dz).type = TileType::Bathroom;
        } else if (type == "food") {
            for (int dz = 0; dz < 2; dz++)
                for (int dx = 0; dx < 2; dx++)
                    if (grid->in_bounds(x + dx, z + dz))
                        grid->at(x + dx, z + dz).type = TileType::Food;
        }
        cmd.consume();
    }
};

// demolish_at X Z
struct HandleDemolishAtCommand : System<testing::PendingE2ECommand> {
    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("demolish_at")) return;
        if (cmd.args.size() < 2) {
            cmd.consume();
            return;
        }
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) {
            cmd.consume();
            return;
        }
        int x = std::stoi(cmd.args[0]), z = std::stoi(cmd.args[1]);
        if (grid->in_bounds(x, z)) {
            auto& tile = grid->at(x, z);
            if (tile.type != TileType::Fence && tile.type != TileType::Grass) {
                tile.type = TileType::Grass;
            }
        }
        cmd.consume();
    }
};

// Register all custom E2E command handler systems
void register_e2e_systems(SystemManager& sm) {
    // Register built-in handlers first
    testing::register_builtin_handlers(sm);

    // Register custom game-specific handlers
    sm.register_update_system(std::make_unique<HandleSpawnAgentCommand>());
    sm.register_update_system(std::make_unique<HandleSpawnAgentsCommand>());
    sm.register_update_system(std::make_unique<HandleClearAgentsCommand>());
    sm.register_update_system(std::make_unique<HandleClearMapCommand>());
    sm.register_update_system(std::make_unique<HandleResetGameCommand>());
    sm.register_update_system(std::make_unique<HandlePlaceFacilityCommand>());
    sm.register_update_system(std::make_unique<HandleSetTileCommand>());
    sm.register_update_system(std::make_unique<HandleGetAgentCountCommand>());
    sm.register_update_system(std::make_unique<HandleGetDensityCommand>());
    sm.register_update_system(
        std::make_unique<HandleAssertAgentCountCommand>());
    sm.register_update_system(std::make_unique<HandleAssertDensityCommand>());
    sm.register_update_system(std::make_unique<HandleAssertTileTypeCommand>());
    sm.register_update_system(std::make_unique<HandleDrawPathRectCommand>());
    sm.register_update_system(std::make_unique<HandleMoveToGridCommand>());
    sm.register_update_system(std::make_unique<HandleClickGridCommand>());
    sm.register_update_system(std::make_unique<HandleAssertAgentNearCommand>());
    sm.register_update_system(std::make_unique<HandleSetSpawnRateCommand>());
    sm.register_update_system(std::make_unique<HandleSetSpawnEnabledCommand>());
    sm.register_update_system(std::make_unique<HandlePlaceGateCommand>());
    sm.register_update_system(std::make_unique<HandleAssertGateCountCommand>());
    sm.register_update_system(std::make_unique<HandleAssertBlockedCommand>());
    sm.register_update_system(std::make_unique<HandleToggleDebugCommand>());
    sm.register_update_system(std::make_unique<HandleTriggerGameOverCommand>());
    sm.register_update_system(
        std::make_unique<HandleAssertGameStatusCommand>());
    sm.register_update_system(std::make_unique<HandleToggleOverlayCommand>());
    sm.register_update_system(std::make_unique<HandleAssertOverlayCommand>());
    sm.register_update_system(std::make_unique<HandleSetAgentSpeedCommand>());
    sm.register_update_system(std::make_unique<HandleGetDeathCountCommand>());
    sm.register_update_system(
        std::make_unique<HandleAssertDeathCountCommand>());
    sm.register_update_system(std::make_unique<HandleAssertAgentHpCommand>());
    sm.register_update_system(std::make_unique<HandleForceNeedCommand>());
    sm.register_update_system(
        std::make_unique<HandleAssertAgentsAtFacilityCommand>());
    sm.register_update_system(
        std::make_unique<HandleAssertAgentWatchingCommand>());
    sm.register_update_system(std::make_unique<HandleSetTimeCommand>());
    sm.register_update_system(std::make_unique<HandleSetSpeedCommand>());
    sm.register_update_system(std::make_unique<HandleAssertPhaseCommand>());
    sm.register_update_system(
        std::make_unique<HandleAssertTimeBetweenCommand>());
    sm.register_update_system(
        std::make_unique<HandleAssertStageStateCommand>());
    sm.register_update_system(std::make_unique<HandleForceArtistCommand>());
    sm.register_update_system(
        std::make_unique<HandleAssertAgentsExitedCommand>());
    sm.register_update_system(std::make_unique<HandleAssertCarryoverCommand>());
    sm.register_update_system(std::make_unique<HandleSetPheromoneCommand>());
    sm.register_update_system(std::make_unique<HandleAssertPheromoneCommand>());
    sm.register_update_system(std::make_unique<HandleClearPheromonesCommand>());
    sm.register_update_system(std::make_unique<HandleSetMaxAttendeesCommand>());
    sm.register_update_system(
        std::make_unique<HandleAssertMaxAttendeesCommand>());
    sm.register_update_system(std::make_unique<HandleAssertSlotsCommand>());
    sm.register_update_system(std::make_unique<HandleSelectToolCommand>());
    sm.register_update_system(std::make_unique<HandleAssertToolCommand>());
    sm.register_update_system(std::make_unique<HandlePlaceBuildingCommand>());
    sm.register_update_system(std::make_unique<HandleDemolishAtCommand>());

    // Unknown handler and cleanup must be last
    testing::register_unknown_handler(sm);
    testing::register_cleanup(sm);
}
