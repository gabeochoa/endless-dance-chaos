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

// Helper: compare with operator string
inline bool compare_op(int actual, const std::string& op, int expected) {
    if (op == "eq" || op == "==") return actual == expected;
    if (op == "gt" || op == ">") return actual > expected;
    if (op == "lt" || op == "<") return actual < expected;
    if (op == "gte" || op == ">=") return actual >= expected;
    if (op == "lte" || op == "<=") return actual <= expected;
    if (op == "ne" || op == "!=") return actual != expected;
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

// spawn_agent X Z TYPE
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
        make_agent(x, z, type);
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
        for (int i = 0; i < count; i++) {
            make_agent(x, z, type);
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

        // Reset grid
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (grid) {
            for (auto& tile : grid->tiles) {
                tile.type = TileType::Grass;
                tile.agent_count = 0;
            }
        }

        // Reset game state
        auto* state = EntityHelper::get_singleton_cmp<GameState>();
        if (state) {
            state->status = GameStatus::Running;
            state->game_time = 0.f;
            state->show_data_layer = false;
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

    // Unknown handler and cleanup must be last
    testing::register_unknown_handler(sm);
    testing::register_cleanup(sm);
}
