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
        for (Entity& agent : agents) {
            auto& needs = agent.get<AgentNeeds>();
            if (type_str == "bathroom") {
                needs.needs_bathroom = true;
            } else if (type_str == "food") {
                needs.needs_food = true;
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
                if (grid->in_bounds(x, z)) {
                    grid->at(x, z).type = TileType::Path;
                }
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

    // Unknown handler and cleanup must be last
    testing::register_unknown_handler(sm);
    testing::register_cleanup(sm);
}
