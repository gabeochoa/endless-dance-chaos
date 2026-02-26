#pragma once

// Custom E2E command handlers for game-specific test commands.
// Uses a dispatch table instead of individual System structs to reduce
// boilerplate (~40 commands, each needs the same guard pattern).

#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "components.h"
#include "entity_makers.h"
#include "game.h"
#include "render_helpers.h"
#include "save_system.h"
#include "systems.h"
#include "update_helpers.h"

#include "afterhours/src/core/entity_helper.h"
#include "afterhours/src/core/entity_query.h"
#include "afterhours/src/plugins/e2e_testing/e2e_testing.h"

using namespace afterhours;

// ── Helpers ──────────────────────────────────────────────────────────────

inline bool compare_op(int actual, const std::string& op, int expected) {
    if (op == "eq" || op == "==") return actual == expected;
    if (op == "gt" || op == ">") return actual > expected;
    if (op == "lt" || op == "<") return actual < expected;
    if (op == "gte" || op == ">=") return actual >= expected;
    if (op == "lte" || op == "<=") return actual <= expected;
    if (op == "ne" || op == "!=") return actual != expected;
    return false;
}

inline bool compare_op_f(float actual, const std::string& op, float expected) {
    if (op == "eq" || op == "==") return std::abs(actual - expected) < 0.001f;
    if (op == "gt" || op == ">") return actual > expected;
    if (op == "lt" || op == "<") return actual < expected;
    if (op == "gte" || op == ">=") return actual >= expected;
    if (op == "lte" || op == "<=") return actual <= expected;
    if (op == "ne" || op == "!=") return std::abs(actual - expected) >= 0.001f;
    return false;
}

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
    if (lower == "medtent" || lower == "med_tent" || lower == "med")
        return TileType::MedTent;
    return TileType::Grass;
}

inline FacilityType parse_facility_type(const std::string& s) {
    std::string lower = s;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower == "bathroom") return FacilityType::Bathroom;
    if (lower == "food") return FacilityType::Food;
    if (lower == "stage") return FacilityType::Stage;
    if (lower == "exit") return FacilityType::Exit;
    if (lower == "medtent" || lower == "med_tent" || lower == "med")
        return FacilityType::MedTent;
    return FacilityType::Bathroom;
}

inline std::optional<vec2> grid_to_screen(int gx, int gz) {
    auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
    if (!cam) return std::nullopt;
    vec3 world_pos = {gx * TILESIZE, 0.0f, gz * TILESIZE};
    return afterhours::get_world_to_screen(world_pos, cam->cam.camera);
}

// ── Performance sampling (needs to be above dispatch system) ─────────────

struct PerfSample {
    float fps_sum = 0.f;
    float fps_min = 9999.f;
    float fps_max = 0.f;
    int sample_count = 0;
    bool is_sampling = false;

    void reset() {
        fps_sum = 0.f;
        fps_min = 9999.f;
        fps_max = 0.f;
        sample_count = 0;
        is_sampling = false;
    }

    void tick() {
        if (!is_sampling) return;
        float fps = get_fps();
        if (fps <= 0.f) return;
        fps_sum += fps;
        fps_min = std::min(fps_min, fps);
        fps_max = std::max(fps_max, fps);
        sample_count++;
    }

    float avg() const {
        return (sample_count > 0) ? fps_sum / sample_count : 0.f;
    }
};

static PerfSample& get_perf_sample() {
    static PerfSample s;
    return s;
}

// ── Command handler type + dispatch registry ─────────────────────────────

using E2ECmdFn = void (*)(testing::PendingE2ECommand&);

struct E2ERegistry {
    std::unordered_map<std::string, E2ECmdFn> handlers;
    void add(const char* name, E2ECmdFn fn) { handlers[name] = fn; }
};

static E2ERegistry& get_registry() {
    static E2ERegistry reg;
    return reg;
}

// Single dispatch system replaces ~40 individual handler structs
struct E2EDispatchSystem : System<testing::PendingE2ECommand> {
    void once(float) override { get_perf_sample().tick(); }

    void for_each_with(Entity&, testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed()) return;
        auto& reg = get_registry();
        auto it = reg.handlers.find(cmd.name);
        if (it != reg.handlers.end()) {
            it->second(cmd);
        }
    }
};

// ── Command handlers (free functions) ────────────────────────────────────

static void cmd_spawn_agent(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(3)) {
        cmd.fail("spawn_agent requires X Z TYPE");
        return;
    }
    int x = cmd.arg_as<int>(0), z = cmd.arg_as<int>(1);
    FacilityType type = parse_facility_type(cmd.arg(2));
    int tx = cmd.has_args(5) ? cmd.arg_as<int>(3) : (STAGE_X + STAGE_SIZE / 2);
    int tz = cmd.has_args(5) ? cmd.arg_as<int>(4) : (STAGE_Z + STAGE_SIZE / 2);
    make_agent(x, z, type, tx, tz);
    EntityHelper::merge_entity_arrays();
    cmd.consume();
}

static void cmd_spawn_agents(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(4)) {
        cmd.fail("spawn_agents requires X Z COUNT TYPE");
        return;
    }
    int x = cmd.arg_as<int>(0), z = cmd.arg_as<int>(1);
    int count = cmd.arg_as<int>(2);
    FacilityType type = parse_facility_type(cmd.arg(3));
    for (int i = 0; i < count; i++) {
        int tx, tz;
        if (type == FacilityType::Stage) {
            auto [sx, sz] = best_stage_spot(x, z);
            tx = sx;
            tz = sz;
        } else {
            tx = STAGE_X + STAGE_SIZE / 2;
            tz = STAGE_Z + STAGE_SIZE / 2;
        }
        make_agent(x, z, type, tx, tz);
    }
    EntityHelper::merge_entity_arrays();
    cmd.consume();
}

static void cmd_clear_agents(testing::PendingE2ECommand& cmd) {
    auto agents = EntityQuery().whereHasComponent<Agent>().gen();
    for (Entity& a : agents) a.cleanup = true;
    EntityHelper::cleanup();
    cmd.consume();
}

static void cmd_clear_map(testing::PendingE2ECommand& cmd) {
    auto agents = EntityQuery().whereHasComponent<Agent>().gen();
    for (Entity& a : agents) a.cleanup = true;
    EntityHelper::cleanup();
    auto* grid = EntityHelper::get_singleton_cmp<Grid>();
    if (grid) {
        for (auto& tile : grid->tiles) {
            tile.type = TileType::Grass;
            tile.agent_count = 0;
        }
        grid->mark_tiles_dirty();
    }
    cmd.consume();
}

static void cmd_reset_game(testing::PendingE2ECommand& cmd) {
    reset_game_state();
    cmd.consume();
}

static void cmd_place_facility(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(3)) {
        cmd.fail("place_facility requires TYPE X Z");
        return;
    }
    TileType type = parse_tile_type(cmd.arg(0));
    int x = cmd.arg_as<int>(1), z = cmd.arg_as<int>(2);
    auto* grid = EntityHelper::get_singleton_cmp<Grid>();
    if (!grid || !grid->in_bounds(x, z)) {
        cmd.fail("place_facility: out of bounds");
        return;
    }
    grid->at(x, z).type = type;
    grid->mark_tiles_dirty();
    cmd.consume();
}

static void cmd_set_tile(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(3)) {
        cmd.fail("set_tile requires X Z TYPE");
        return;
    }
    int x = cmd.arg_as<int>(0), z = cmd.arg_as<int>(1);
    TileType type = parse_tile_type(cmd.arg(2));
    auto* grid = EntityHelper::get_singleton_cmp<Grid>();
    if (!grid || !grid->in_bounds(x, z)) {
        cmd.fail("set_tile: out of bounds");
        return;
    }
    grid->at(x, z).type = type;
    grid->mark_tiles_dirty();
    cmd.consume();
}

static void cmd_get_agent_count(testing::PendingE2ECommand& cmd) {
    int count = (int) EntityQuery().whereHasComponent<Agent>().gen_count();
    log_info("[E2E] Agent count: {}", count);
    cmd.consume();
}

static void cmd_get_density(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("get_density requires X Z");
        return;
    }
    int x = cmd.arg_as<int>(0), z = cmd.arg_as<int>(1);
    auto* grid = EntityHelper::get_singleton_cmp<Grid>();
    if (!grid || !grid->in_bounds(x, z)) {
        cmd.fail("get_density: out of bounds");
        return;
    }
    log_info("[E2E] Density at ({}, {}): {}", x, z, grid->at(x, z).agent_count);
    cmd.consume();
}

static void cmd_assert_agent_count(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_agent_count requires OP VALUE");
        return;
    }
    int actual = (int) EntityQuery().whereHasComponent<Agent>().gen_count();
    if (!compare_op(actual, cmd.arg(0), cmd.arg_as<int>(1)))
        cmd.fail(fmt::format("assert_agent_count failed: {} {} {} (actual: {})",
                             actual, cmd.arg(0), cmd.arg_as<int>(1), actual));
    else
        cmd.consume();
}

static void cmd_assert_density(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(4)) {
        cmd.fail("assert_density requires X Z OP VALUE");
        return;
    }
    int x = cmd.arg_as<int>(0), z = cmd.arg_as<int>(1);
    auto* grid = EntityHelper::get_singleton_cmp<Grid>();
    if (!grid || !grid->in_bounds(x, z)) {
        cmd.fail("assert_density: out of bounds");
        return;
    }
    int actual = grid->at(x, z).agent_count;
    if (!compare_op(actual, cmd.arg(2), cmd.arg_as<int>(3)))
        cmd.fail(fmt::format(
            "assert_density at ({},{}) failed: {} {} {} (actual: {})", x, z,
            actual, cmd.arg(2), cmd.arg_as<int>(3), actual));
    else
        cmd.consume();
}

static void cmd_assert_tile_type(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(3)) {
        cmd.fail("assert_tile_type requires X Z TYPE");
        return;
    }
    int x = cmd.arg_as<int>(0), z = cmd.arg_as<int>(1);
    TileType expected = parse_tile_type(cmd.arg(2));
    auto* grid = EntityHelper::get_singleton_cmp<Grid>();
    if (!grid || !grid->in_bounds(x, z)) {
        cmd.fail("assert_tile_type: out of bounds");
        return;
    }
    TileType actual = grid->at(x, z).type;
    if (actual != expected)
        cmd.fail(fmt::format(
            "assert_tile_type at ({},{}) failed: expected {}, got {}", x, z,
            cmd.arg(2), static_cast<int>(actual)));
    else
        cmd.consume();
}

static void cmd_draw_path_rect(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(4)) {
        cmd.fail("draw_path_rect requires X1 Z1 X2 Z2");
        return;
    }
    int x1 = cmd.arg_as<int>(0), z1 = cmd.arg_as<int>(1);
    int x2 = cmd.arg_as<int>(2), z2 = cmd.arg_as<int>(3);
    auto* grid = EntityHelper::get_singleton_cmp<Grid>();
    if (!grid) {
        cmd.fail("draw_path_rect: no grid");
        return;
    }
    int min_x = std::min(x1, x2), min_z = std::min(z1, z2);
    int max_x = std::max(x1, x2), max_z = std::max(z1, z2);
    for (int z = min_z; z <= max_z; z++)
        for (int x = min_x; x <= max_x; x++)
            if (grid->in_bounds(x, z) && grid->at(x, z).type == TileType::Grass)
                grid->at(x, z).type = TileType::Path;
    grid->mark_tiles_dirty();
    cmd.consume();
}

static void cmd_move_to_grid(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("move_to_grid requires X Z");
        return;
    }
    auto screen = grid_to_screen(cmd.arg_as<int>(0), cmd.arg_as<int>(1));
    if (!screen) {
        cmd.fail("move_to_grid: no camera");
        return;
    }
    testing::test_input::set_mouse_position(screen->x, screen->y);
    cmd.consume();
}

static void cmd_click_grid(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("click_grid requires X Z");
        return;
    }
    int gx = cmd.arg_as<int>(0), gz = cmd.arg_as<int>(1);
    auto* grid = EntityHelper::get_singleton_cmp<Grid>();
    if (!grid || !grid->in_bounds(gx, gz)) {
        cmd.fail("click_grid: out of bounds");
        return;
    }
    auto* pds = EntityHelper::get_singleton_cmp<PathDrawState>();
    if (!pds) {
        cmd.fail("click_grid: no PathDrawState");
        return;
    }
    auto screen = grid_to_screen(gx, gz);
    if (screen)
        testing::test_input::simulate_click(screen->x, screen->y);
    else
        testing::test_input::simulate_mouse_press();
    pds->hover_x = gx;
    pds->hover_z = gz;
    pds->hover_valid = true;
    pds->hover_lock_frames = 2;
    log_info("[E2E] click_grid ({}, {}) injected mouse press", gx, gz);
    cmd.consume();
}

static void cmd_assert_agent_near(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(3)) {
        cmd.fail("assert_agent_near requires X Z RADIUS");
        return;
    }
    int gx = cmd.arg_as<int>(0), gz = cmd.arg_as<int>(1);
    float radius = cmd.arg_as<float>(2);
    auto* grid = EntityHelper::get_singleton_cmp<Grid>();
    if (!grid) {
        cmd.fail("assert_agent_near: no grid");
        return;
    }
    ::vec2 tw = grid->grid_to_world(gx, gz);
    float rw = radius * TILESIZE;
    auto agents = EntityQuery()
                      .whereHasComponent<Agent>()
                      .whereHasComponent<Transform>()
                      .gen();
    for (Entity& a : agents) {
        auto& tf = a.get<Transform>();
        float dx = tf.position.x - tw.x, dz = tf.position.y - tw.y;
        if (std::sqrt(dx * dx + dz * dz) <= rw) {
            cmd.consume();
            return;
        }
    }
    int count = (int) EntityQuery().whereHasComponent<Agent>().gen_count();
    cmd.fail(fmt::format(
        "assert_agent_near ({},{}) r={}: no agent nearby ({} total)", gx, gz,
        radius, count));
}

static void cmd_set_spawn_rate(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("set_spawn_rate requires INTERVAL");
        return;
    }
    auto* ss = EntityHelper::get_singleton_cmp<SpawnState>();
    if (!ss) {
        cmd.fail("set_spawn_rate: no SpawnState");
        return;
    }
    ss->interval = cmd.arg_as<float>(0);
    cmd.consume();
}

static void cmd_set_spawn_enabled(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("set_spawn_enabled requires 0|1");
        return;
    }
    auto* ss = EntityHelper::get_singleton_cmp<SpawnState>();
    if (!ss) {
        cmd.fail("set_spawn_enabled: no SpawnState");
        return;
    }
    ss->enabled = (cmd.arg_as<int>(0) != 0);
    cmd.consume();
}

static void cmd_force_need(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("force_need requires TYPE");
        return;
    }
    std::string type_str = cmd.arg(0);
    std::transform(type_str.begin(), type_str.end(), type_str.begin(),
                   ::tolower);
    auto agents = EntityQuery()
                      .whereHasComponent<Agent>()
                      .whereHasComponent<AgentNeeds>()
                      .gen();
    for (Entity& a : agents) {
        auto& needs = a.get<AgentNeeds>();
        if (type_str == "bathroom")
            needs.needs_bathroom = true;
        else if (type_str == "food")
            needs.needs_food = true;
        if (!a.is_missing<WatchingStage>()) a.removeComponent<WatchingStage>();
    }
    cmd.consume();
}

static void cmd_assert_agents_at_facility(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(3)) {
        cmd.fail("assert_agents_at_facility requires TYPE OP COUNT");
        return;
    }
    FacilityType ftype = parse_facility_type(cmd.arg(0));
    int count = 0;
    auto agents = EntityQuery()
                      .whereHasComponent<Agent>()
                      .whereHasComponent<BeingServiced>()
                      .gen();
    for (Entity& a : agents)
        if (a.get<BeingServiced>().facility_type == ftype) count++;
    if (!compare_op(count, cmd.arg(1), cmd.arg_as<int>(2)))
        cmd.fail(fmt::format(
            "assert_agents_at_facility {} failed: {} {} {} (actual: {})",
            cmd.arg(0), count, cmd.arg(1), cmd.arg_as<int>(2), count));
    else
        cmd.consume();
}

static void cmd_assert_agent_watching(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_agent_watching requires OP COUNT");
        return;
    }
    int count =
        (int) EntityQuery().whereHasComponent<WatchingStage>().gen_count();
    if (!compare_op(count, cmd.arg(0), cmd.arg_as<int>(1)))
        cmd.fail(
            fmt::format("assert_agent_watching failed: {} {} {} (actual: {})",
                        count, cmd.arg(0), cmd.arg_as<int>(1), count));
    else
        cmd.consume();
}

static void cmd_assert_agents_on_tiletype(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(3)) {
        cmd.fail("assert_agents_on_tiletype requires TYPE OP COUNT");
        return;
    }
    TileType type = parse_tile_type(cmd.arg(0));
    auto* grid = EntityHelper::get_singleton_cmp<Grid>();
    if (!grid) {
        cmd.fail("assert_agents_on_tiletype: no grid");
        return;
    }
    int count = 0;
    auto agents = EntityQuery()
                      .whereHasComponent<Agent>()
                      .whereHasComponent<Transform>()
                      .gen();
    for (Entity& a : agents) {
        auto& tf = a.get<Transform>();
        auto [gx, gz] = grid->world_to_grid(tf.position.x, tf.position.y);
        if (grid->in_bounds(gx, gz) && grid->at(gx, gz).type == type) count++;
    }
    if (!compare_op(count, cmd.arg(1), cmd.arg_as<int>(2)))
        cmd.fail(fmt::format(
            "assert_agents_on_tiletype {} failed: {} {} {} (actual: {})",
            cmd.arg(0), count, cmd.arg(1), cmd.arg_as<int>(2), count));
    else {
        log_info("[E2E] assert_agents_on_tiletype {}: {} {} {} PASSED",
                 cmd.arg(0), count, cmd.arg(1), cmd.arg_as<int>(2));
        cmd.consume();
    }
}

static void cmd_place_gate(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("place_gate requires X Z");
        return;
    }
    int x = cmd.arg_as<int>(0), z = cmd.arg_as<int>(1);
    auto* grid = EntityHelper::get_singleton_cmp<Grid>();
    if (!grid) {
        cmd.fail("place_gate: no grid");
        return;
    }
    if (grid->in_bounds(x, z)) grid->at(x, z).type = TileType::Gate;
    if (grid->in_bounds(x, z + 1)) grid->at(x, z + 1).type = TileType::Gate;
    grid->mark_tiles_dirty();
    cmd.consume();
}

static void cmd_assert_gate_count(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_gate_count requires OP VALUE");
        return;
    }
    auto* grid = EntityHelper::get_singleton_cmp<Grid>();
    if (!grid) {
        cmd.fail("assert_gate_count: no grid");
        return;
    }
    int count = 0;
    for (const auto& tile : grid->tiles)
        if (tile.type == TileType::Gate) count++;
    if (!compare_op(count, cmd.arg(0), cmd.arg_as<int>(1)))
        cmd.fail(fmt::format("assert_gate_count failed: {} {} {} (actual: {})",
                             count, cmd.arg(0), cmd.arg_as<int>(1), count));
    else
        cmd.consume();
}

static void cmd_assert_blocked(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_blocked requires X Z");
        return;
    }
    int x = cmd.arg_as<int>(0), z = cmd.arg_as<int>(1);
    auto* grid = EntityHelper::get_singleton_cmp<Grid>();
    if (!grid || !grid->in_bounds(x, z)) {
        cmd.fail("assert_blocked: out of bounds");
        return;
    }
    if (grid->at(x, z).type != TileType::Fence)
        cmd.fail(
            fmt::format("assert_blocked ({},{}) failed: not fence (type={})", x,
                        z, static_cast<int>(grid->at(x, z).type)));
    else
        cmd.consume();
}

static void cmd_toggle_debug(testing::PendingE2ECommand& cmd) {
    auto* gs = EntityHelper::get_singleton_cmp<GameState>();
    if (!gs) {
        cmd.fail("toggle_debug: no GameState");
        return;
    }
    gs->show_debug = !gs->show_debug;
    log_info("[E2E] Debug panel: {}", gs->show_debug ? "ON" : "OFF");
    cmd.consume();
}

static void cmd_trigger_game_over(testing::PendingE2ECommand& cmd) {
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

static void cmd_assert_game_status(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("assert_game_status requires STATUS");
        return;
    }
    auto* gs = EntityHelper::get_singleton_cmp<GameState>();
    if (!gs) {
        cmd.fail("assert_game_status: no GameState");
        return;
    }
    bool want_go = (cmd.arg(0) == "gameover" || cmd.arg(0) == "game_over");
    if (want_go != gs->is_game_over())
        cmd.fail(fmt::format("assert_game_status failed: expected {}, got {}",
                             cmd.arg(0),
                             gs->is_game_over() ? "gameover" : "running"));
    else
        cmd.consume();
}

static void cmd_toggle_overlay(testing::PendingE2ECommand& cmd) {
    auto* gs = EntityHelper::get_singleton_cmp<GameState>();
    if (!gs) {
        cmd.fail("toggle_overlay: no GameState");
        return;
    }
    gs->show_data_layer = !gs->show_data_layer;
    log_info("[E2E] Overlay toggled: {}", gs->show_data_layer ? "ON" : "OFF");
    cmd.consume();
}

static void cmd_assert_overlay(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("assert_overlay requires on/off");
        return;
    }
    auto* gs = EntityHelper::get_singleton_cmp<GameState>();
    if (!gs) {
        cmd.fail("assert_overlay: no GameState");
        return;
    }
    bool want_on = (cmd.arg(0) == "on" || cmd.arg(0) == "1");
    if (gs->show_data_layer != want_on)
        cmd.fail(fmt::format("assert_overlay failed: expected {}, got {}",
                             cmd.arg(0), gs->show_data_layer ? "on" : "off"));
    else
        cmd.consume();
}

static void cmd_set_agent_speed(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("set_agent_speed requires MULTIPLIER");
        return;
    }
    auto* gs = EntityHelper::get_singleton_cmp<GameState>();
    if (!gs) {
        cmd.fail("set_agent_speed: no GameState");
        return;
    }
    float mult = cmd.arg_as<float>(0);
    gs->speed_multiplier = mult;

    // Also speed up the game clock so spawning, needs, crush damage,
    // artist timing, etc. all run faster — not just walking speed.
    auto* clock = EntityHelper::get_singleton_cmp<GameClock>();
    if (clock) {
        clock->debug_time_mult = mult;
    }

    log_info("[E2E] Agent speed multiplier set to {} (game clock too)",
             gs->speed_multiplier);
    cmd.consume();
}

static void cmd_set_move_speed(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("set_move_speed requires MULTIPLIER");
        return;
    }
    auto* gs = EntityHelper::get_singleton_cmp<GameState>();
    if (!gs) {
        cmd.fail("set_move_speed: no GameState");
        return;
    }
    gs->speed_multiplier = cmd.arg_as<float>(0);
    log_info("[E2E] Move speed multiplier set to {} (clock unchanged)",
             gs->speed_multiplier);
    cmd.consume();
}

static void cmd_get_death_count(testing::PendingE2ECommand& cmd) {
    auto* gs = EntityHelper::get_singleton_cmp<GameState>();
    log_info("[E2E] Death count: {}/{}", gs ? gs->death_count : 0,
             gs ? gs->max_deaths : 0);
    cmd.consume();
}

static void cmd_set_death_count(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("set_death_count requires VALUE");
        return;
    }
    auto* gs = EntityHelper::get_singleton_cmp<GameState>();
    if (gs) {
        gs->death_count = cmd.arg_as<int>(0);
        log_info("[E2E] set_death_count: {}", gs->death_count);
    }
    cmd.consume();
}

static void cmd_assert_death_count(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_death_count requires OP VALUE");
        return;
    }
    auto* gs = EntityHelper::get_singleton_cmp<GameState>();
    int actual = gs ? gs->death_count : 0;
    if (!compare_op(actual, cmd.arg(0), cmd.arg_as<int>(1)))
        cmd.fail(fmt::format("assert_death_count failed: {} {} {} (actual: {})",
                             actual, cmd.arg(0), cmd.arg_as<int>(1), actual));
    else
        cmd.consume();
}

static void cmd_assert_agent_hp(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(4)) {
        cmd.fail("assert_agent_hp requires X Z OP VALUE");
        return;
    }
    int tx = cmd.arg_as<int>(0), tz = cmd.arg_as<int>(1);
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
    bool found = false;
    for (Entity& a : agents) {
        auto [gx, gz] = grid->world_to_grid(a.get<Transform>().position.x,
                                            a.get<Transform>().position.y);
        if (gx == tx && gz == tz) {
            found = true;
            float actual = a.get<AgentHealth>().hp;
            if (!compare_op_f(actual, cmd.arg(2), cmd.arg_as<float>(3))) {
                cmd.fail(fmt::format(
                    "assert_agent_hp at ({},{}) failed: {:.3f} {} {}", tx, tz,
                    actual, cmd.arg(2), cmd.arg_as<float>(3)));
                return;
            }
        }
    }
    if (!found) {
        cmd.fail(fmt::format("assert_agent_hp: no agents at ({},{})", tx, tz));
        return;
    }
    cmd.consume();
}

static void cmd_set_time(testing::PendingE2ECommand& cmd) {
    if (cmd.args.size() < 2) {
        log_warn("set_time requires HOUR MINUTE");
        cmd.consume();
        return;
    }
    int hour = std::stoi(cmd.args[0]), minute = std::stoi(cmd.args[1]);
    auto* clock = EntityHelper::get_singleton_cmp<GameClock>();
    if (clock) {
        clock->game_time_minutes = static_cast<float>(hour * 60 + minute);
        log_info("set_time: {:02d}:{:02d} ({} minutes)", hour, minute,
                 clock->game_time_minutes);
    }
    cmd.consume();
}

static void cmd_set_speed(testing::PendingE2ECommand& cmd) {
    if (cmd.args.empty()) {
        log_warn("set_speed requires SPEED");
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
            log_warn("set_speed: unknown '{}'", s);
        log_info("set_speed: {}", s);
    }
    cmd.consume();
}

static void cmd_assert_phase(testing::PendingE2ECommand& cmd) {
    if (cmd.args.empty()) {
        log_warn("assert_phase requires PHASE");
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
    auto phase = clock->get_phase();
    std::string actual = GameClock::phase_name(phase);
    std::string actual_lower = actual;
    std::transform(actual_lower.begin(), actual_lower.end(),
                   actual_lower.begin(), ::tolower);
    bool match = (actual_lower == expected) ||
                 (expected == "dead" && phase == GameClock::Phase::DeadHours);
    if (!match)
        cmd.fail(fmt::format("assert_phase FAILED: expected '{}', got '{}'",
                             expected, actual));
    else {
        log_info("assert_phase PASSED: phase is '{}'", actual);
        cmd.consume();
    }
}

static void cmd_assert_time_between(testing::PendingE2ECommand& cmd) {
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
        log_info("assert_time_between PASSED: {} in [{}, {}]", current, t1, t2);
        cmd.consume();
    } else {
        cmd.fail(fmt::format("assert_time_between FAILED: {} not in [{}, {}]",
                             current, t1, t2));
    }
}

static void cmd_assert_stage_state(testing::PendingE2ECommand& cmd) {
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
    static const char* names[] = {"idle", "announcing", "performing",
                                  "clearing"};
    std::string actual = names[static_cast<int>(sched->stage_state)];
    if (actual == expected) {
        log_info("assert_stage_state PASSED: {}", actual);
        cmd.consume();
    } else
        cmd.fail(
            fmt::format("assert_stage_state FAILED: expected '{}', got '{}'",
                        expected, actual));
}

static void cmd_force_artist(testing::PendingE2ECommand& cmd) {
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
    a.start_time_minutes =
        (cmd.args.size() >= 5)
            ? static_cast<float>(std::stoi(cmd.args[3]) * 60 +
                                 std::stoi(cmd.args[4]))
            : clock->game_time_minutes + 1.f;
    sched->schedule.insert(sched->schedule.begin(), a);
    log_info("force_artist: '{}' crowd={} dur={}", a.name, a.expected_crowd,
             a.duration_minutes);
    cmd.consume();
}

static void cmd_assert_agents_exited(testing::PendingE2ECommand& cmd) {
    if (cmd.args.size() < 2) {
        cmd.consume();
        return;
    }
    auto* gs = EntityHelper::get_singleton_cmp<GameState>();
    if (!gs) {
        cmd.consume();
        return;
    }
    if (compare_op(gs->agents_exited, cmd.args[0], std::stoi(cmd.args[1]))) {
        log_info("assert_agents_exited PASSED: {} {} {}", gs->agents_exited,
                 cmd.args[0], cmd.args[1]);
        cmd.consume();
    } else {
        cmd.fail(fmt::format(
            "assert_agents_exited FAILED: {} {} {} (actual: {})",
            gs->agents_exited, cmd.args[0], cmd.args[1], gs->agents_exited));
    }
}

static void cmd_assert_carryover_count(testing::PendingE2ECommand& cmd) {
    if (cmd.args.size() < 2) {
        cmd.consume();
        return;
    }
    auto* gs = EntityHelper::get_singleton_cmp<GameState>();
    if (!gs) {
        cmd.consume();
        return;
    }
    if (compare_op(gs->carryover_count, cmd.args[0], std::stoi(cmd.args[1]))) {
        log_info("assert_carryover_count PASSED: {}", gs->carryover_count);
        cmd.consume();
    } else {
        cmd.fail(fmt::format("assert_carryover_count FAILED: actual={}",
                             gs->carryover_count));
    }
}

static void cmd_set_pheromone(testing::PendingE2ECommand& cmd) {
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
    int ch = std::stoi(cmd.args[2]), val = std::stoi(cmd.args[3]);
    if (grid->in_bounds(x, z) && ch >= 0 && ch < 5)
        grid->at(x, z).pheromone[ch] =
            static_cast<uint8_t>(std::clamp(val, 0, 255));
    cmd.consume();
}

static void cmd_assert_pheromone(testing::PendingE2ECommand& cmd) {
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
    if (grid->in_bounds(x, z) && ch >= 0 && ch < 5) {
        int actual = grid->at(x, z).pheromone[ch];
        if (compare_op(actual, cmd.args[3], std::stoi(cmd.args[4]))) {
            log_info("assert_pheromone PASSED: ({},{}) ch={} val={}", x, z, ch,
                     actual);
            cmd.consume();
        } else {
            cmd.fail(fmt::format(
                "assert_pheromone FAILED: ({},{}) ch={} actual={} {} {}", x, z,
                ch, actual, cmd.args[3], cmd.args[4]));
        }
        return;
    }
    cmd.consume();
}

static void cmd_clear_pheromones(testing::PendingE2ECommand& cmd) {
    auto* grid = EntityHelper::get_singleton_cmp<Grid>();
    if (grid)
        for (auto& tile : grid->tiles) tile.pheromone = {0, 0, 0, 0, 0};
    cmd.consume();
}

static void cmd_set_max_attendees(testing::PendingE2ECommand& cmd) {
    if (cmd.args.empty()) {
        cmd.consume();
        return;
    }
    auto* gs = EntityHelper::get_singleton_cmp<GameState>();
    if (gs) gs->max_attendees = std::stoi(cmd.args[0]);
    cmd.consume();
}

static void cmd_assert_max_attendees(testing::PendingE2ECommand& cmd) {
    if (cmd.args.size() < 2) {
        cmd.consume();
        return;
    }
    auto* gs = EntityHelper::get_singleton_cmp<GameState>();
    if (!gs) {
        cmd.consume();
        return;
    }
    if (compare_op(gs->max_attendees, cmd.args[0], std::stoi(cmd.args[1]))) {
        log_info("assert_max_attendees PASSED: {}", gs->max_attendees);
        cmd.consume();
    } else {
        cmd.fail(fmt::format("assert_max_attendees FAILED: actual={}",
                             gs->max_attendees));
    }
}

static void cmd_assert_slots(testing::PendingE2ECommand& cmd) {
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
        cmd.fail(fmt::format("assert_slots FAILED: {} actual={}", cmd.args[0],
                             slots));
    }
}

static void cmd_select_tool(testing::PendingE2ECommand& cmd) {
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
    else if (tool == "medtent" || tool == "med")
        bs->tool = BuildTool::MedTent;
    else if (tool == "demolish")
        bs->tool = BuildTool::Demolish;
    else
        log_warn("select_tool: unknown tool '{}'", tool);
    cmd.consume();
}

static void cmd_assert_tool(testing::PendingE2ECommand& cmd) {
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
                                  "bathroom", "food",  "medtent", "demolish"};
    int idx = static_cast<int>(bs->tool);
    std::string actual = (idx >= 0 && idx < 8) ? names[idx] : "unknown";
    if (actual == expected) {
        log_info("assert_tool PASSED: {}", actual);
        cmd.consume();
    } else
        cmd.fail(fmt::format("assert_tool FAILED: expected '{}', got '{}'",
                             expected, actual));
}

static void cmd_place_building(testing::PendingE2ECommand& cmd) {
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
        grid->place_footprint(x, z, 1, 2, TileType::Gate);
    } else if (type == "stage") {
        grid->place_footprint(x, z, 4, 4, TileType::Stage);
    } else if (type == "bathroom") {
        grid->place_footprint(x, z, 2, 2, TileType::Bathroom);
    } else if (type == "food") {
        grid->place_footprint(x, z, 2, 2, TileType::Food);
    } else if (type == "medtent" || type == "med") {
        grid->place_footprint(x, z, 2, 2, TileType::MedTent);
    }
    cmd.consume();
}

static void cmd_demolish_at(testing::PendingE2ECommand& cmd) {
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
        if (tile.type != TileType::Fence && tile.type != TileType::Grass &&
            tile.type != TileType::Gate) {
            tile.type = TileType::Grass;
            grid->mark_tiles_dirty();
        }
    }
    cmd.consume();
}

static void cmd_set_all_agent_hp(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("set_all_agent_hp requires HP_VALUE");
        return;
    }
    float hp = cmd.arg_as<float>(0);
    auto agents = EntityQuery()
                      .whereHasComponent<Agent>()
                      .whereHasComponent<AgentHealth>()
                      .gen();
    int count = 0;
    for (Entity& a : agents) {
        a.get<AgentHealth>().hp = hp;
        count++;
    }
    log_info("[E2E] set_all_agent_hp: set {} agents to hp={:.2f}", count, hp);
    cmd.consume();
}

// ── Performance measurement commands ─────────────────────────────────────

static void cmd_perf_start(testing::PendingE2ECommand& cmd) {
    auto& s = get_perf_sample();
    s.reset();
    s.is_sampling = true;
    log_info("[E2E] perf_start: sampling FPS every frame");
    cmd.consume();
}

static void cmd_perf_report(testing::PendingE2ECommand& cmd) {
    auto& s = get_perf_sample();
    s.is_sampling = false;
    int agent_count =
        (int) EntityQuery().whereHasComponent<Agent>().gen_count();
    log_info(
        "[PERF] agents={} fps: avg={:.1f} min={:.1f} max={:.1f} samples={}",
        agent_count, s.avg(), s.fps_min, s.fps_max, s.sample_count);
    cmd.consume();
}

static void cmd_assert_fps(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_fps requires OP VALUE (e.g. assert_fps gte 30)");
        return;
    }
    auto& s = get_perf_sample();
    float average = s.avg();
    float expected = cmd.arg_as<float>(1);
    if (!compare_op_f(average, cmd.arg(0), expected))
        cmd.fail(fmt::format(
            "[PERF] assert_fps FAILED: avg={:.1f} {} {:.1f} (min={:.1f} "
            "max={:.1f} samples={})",
            average, cmd.arg(0), expected, s.fps_min, s.fps_max,
            s.sample_count));
    else {
        log_info("[PERF] assert_fps PASSED: avg={:.1f} {} {:.1f}", average,
                 cmd.arg(0), expected);
        cmd.consume();
    }
}

static void cmd_assert_min_fps(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_min_fps requires OP VALUE");
        return;
    }
    auto& s = get_perf_sample();
    float expected = cmd.arg_as<float>(1);
    if (!compare_op_f(s.fps_min, cmd.arg(0), expected))
        cmd.fail(
            fmt::format("[PERF] assert_min_fps FAILED: min={:.1f} {} {:.1f}",
                        s.fps_min, cmd.arg(0), expected));
    else {
        log_info("[PERF] assert_min_fps PASSED: min={:.1f} {} {:.1f}",
                 s.fps_min, cmd.arg(0), expected);
        cmd.consume();
    }
}

// ── NUX hint commands ─────────────────────────────────────────────────────

// assert_nux_active <substring> — assert a NUX containing the substring is
// active
static void cmd_assert_nux_active(testing::PendingE2ECommand& cmd) {
    std::string needle = cmd.arg(0);
    auto nuxes = EntityQuery().whereHasComponent<NuxHint>().gen();
    for (Entity& e : nuxes) {
        auto& nux = e.get<NuxHint>();
        if (nux.is_active && nux.text.find(needle) != std::string::npos) {
            log_info(
                "assert_nux_active PASSED: found active NUX containing '{}'",
                needle);
            cmd.consume();
            return;
        }
    }
    cmd.fail(fmt::format("No active NUX containing '{}'", needle));
}

// assert_nux_inactive — assert no NUX is currently active
static void cmd_assert_nux_inactive(testing::PendingE2ECommand& cmd) {
    auto nuxes = EntityQuery().whereHasComponent<NuxHint>().gen();
    for (Entity& e : nuxes) {
        if (e.get<NuxHint>().is_active) {
            cmd.fail(
                fmt::format("NUX still active: '{}'", e.get<NuxHint>().text));
            return;
        }
    }
    log_info("assert_nux_inactive PASSED: no active NUX");
    cmd.consume();
}

// assert_nux_count <op> <n> — assert number of remaining (non-dismissed) NUX
// entities
static void cmd_assert_nux_count(testing::PendingE2ECommand& cmd) {
    std::string op = cmd.arg(0);
    int expected = std::stoi(cmd.arg(1));
    int count = 0;
    {
        auto nuxes = EntityQuery().whereHasComponent<NuxHint>().gen();
        for (Entity& e : nuxes) {
            if (!e.get<NuxHint>().was_dismissed) count++;
        }
    }
    bool ok = false;
    if (op == "eq")
        ok = (count == expected);
    else if (op == "gte")
        ok = (count >= expected);
    else if (op == "lte")
        ok = (count <= expected);
    else if (op == "lt")
        ok = (count < expected);
    if (!ok) {
        cmd.fail(fmt::format("NUX count {} not {} {}", count, op, expected));
        return;
    }
    log_info("assert_nux_count PASSED: {} {} {}", count, op, expected);
    cmd.consume();
}

// dismiss_nux — dismiss the currently active NUX (simulates clicking X)
static void cmd_dismiss_nux(testing::PendingE2ECommand& cmd) {
    auto nuxes = EntityQuery().whereHasComponent<NuxHint>().gen();
    for (Entity& e : nuxes) {
        auto& nux = e.get<NuxHint>();
        if (nux.is_active) {
            nux.was_dismissed = true;
            log_info("dismiss_nux: dismissed '{}'", nux.text);
            cmd.consume();
            return;
        }
    }
    log_info("dismiss_nux: no active NUX to dismiss");
    cmd.consume();
}

// assert_pixel X Y not R G B A
//   Samples pixel at (X,Y) from the main render texture and asserts it does
//   NOT match the given RGBA color (within tolerance). Useful for verifying
//   that 3D geometry or UI actually rendered (pixel is not just background).
//
// assert_pixel X Y is R G B A
//   Asserts the pixel DOES match the given RGBA color.
//
// On Metal backend this is currently a no-op (pixel readback not available).
static void cmd_assert_pixel(testing::PendingE2ECommand& cmd) {
    if (cmd.args.size() < 7) {
        cmd.fail("assert_pixel requires X Y is|not R G B A");
        return;
    }
    int px = std::stoi(cmd.args[0]);
    int py = std::stoi(cmd.args[1]);
    std::string op = cmd.args[2];
    int er = std::stoi(cmd.args[3]);
    int eg = std::stoi(cmd.args[4]);
    int eb = std::stoi(cmd.args[5]);
    int ea = std::stoi(cmd.args[6]);

#ifdef AFTER_HOURS_USE_METAL
    log_warn("assert_pixel: skipped on Metal (no pixel readback)");
    cmd.consume();
    return;
#else
    raylib::Image img = raylib::LoadImageFromTexture(g_render_texture.texture);
    raylib::ImageFlipVertical(&img);

    if (px < 0 || px >= img.width || py < 0 || py >= img.height) {
        raylib::UnloadImage(img);
        cmd.fail(fmt::format("assert_pixel: ({},{}) out of bounds ({}x{})", px,
                             py, img.width, img.height));
        return;
    }

    raylib::Color c = raylib::GetImageColor(img, px, py);
    raylib::UnloadImage(img);

    constexpr int TOLERANCE = 15;
    bool matches =
        std::abs(c.r - er) <= TOLERANCE && std::abs(c.g - eg) <= TOLERANCE &&
        std::abs(c.b - eb) <= TOLERANCE && std::abs(c.a - ea) <= TOLERANCE;

    if (op == "is" && !matches) {
        cmd.fail(fmt::format(
            "assert_pixel ({},{}) is ({},{},{},{}) FAILED: got ({},{},{},{})",
            px, py, er, eg, eb, ea, c.r, c.g, c.b, c.a));
        return;
    }
    if (op == "not" && matches) {
        cmd.fail(fmt::format(
            "assert_pixel ({},{}) not ({},{},{},{}) FAILED: pixel matches", px,
            py, er, eg, eb, ea));
        return;
    }

    log_info(
        "assert_pixel PASSED: ({},{}) {} ({},{},{},{}) actual=({},{},{},{})",
        px, py, op, er, eg, eb, ea, c.r, c.g, c.b, c.a);
    cmd.consume();
#endif
}

// assert_region_not_blank X Y W H
//   Samples a region and asserts not all pixels are the same color.
//   Catches cases where 3D rendering is completely missing.
static void cmd_assert_region_not_blank(testing::PendingE2ECommand& cmd) {
    if (cmd.args.size() < 4) {
        cmd.fail("assert_region_not_blank requires X Y W H");
        return;
    }
    int rx = std::stoi(cmd.args[0]);
    int ry = std::stoi(cmd.args[1]);
    int rw = std::stoi(cmd.args[2]);
    int rh = std::stoi(cmd.args[3]);

#ifdef AFTER_HOURS_USE_METAL
    log_warn("assert_region_not_blank: skipped on Metal (no pixel readback)");
    cmd.consume();
    return;
#else
    raylib::Image img = raylib::LoadImageFromTexture(g_render_texture.texture);
    raylib::ImageFlipVertical(&img);

    int x_end = std::min(rx + rw, img.width);
    int y_end = std::min(ry + rh, img.height);
    if (rx >= img.width || ry >= img.height || x_end <= rx || y_end <= ry) {
        raylib::UnloadImage(img);
        cmd.fail(fmt::format(
            "assert_region_not_blank: region ({},{} {}x{}) out of bounds", rx,
            ry, rw, rh));
        return;
    }

    raylib::Color first = raylib::GetImageColor(img, rx, ry);
    bool all_same = true;
    int sample_step = std::max(1, std::min(rw, rh) / 10);

    for (int y = ry; y < y_end && all_same; y += sample_step) {
        for (int x = rx; x < x_end && all_same; x += sample_step) {
            raylib::Color c = raylib::GetImageColor(img, x, y);
            if (std::abs(c.r - first.r) > 5 || std::abs(c.g - first.g) > 5 ||
                std::abs(c.b - first.b) > 5) {
                all_same = false;
            }
        }
    }
    raylib::UnloadImage(img);

    if (all_same) {
        cmd.fail(fmt::format(
            "assert_region_not_blank FAILED: region ({},{} {}x{}) is all "
            "({},{},{},{})",
            rx, ry, rw, rh, first.r, first.g, first.b, first.a));
        return;
    }

    log_info("assert_region_not_blank PASSED: ({},{} {}x{}) has varied content",
             rx, ry, rw, rh);
    cmd.consume();
#endif
}

static void cmd_set_zoom(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("set_zoom requires FOVY");
        return;
    }
    float fovy = cmd.arg_as<float>(0);
    auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
    if (!cam) {
        cmd.fail("set_zoom: no camera");
        return;
    }
    cam->cam.distance = fovy;
    cam->cam.camera.fovy = fovy;
    cam->cam.update_camera_position();
    log_info("[E2E] set_zoom: fovy={}", fovy);
    cmd.consume();
}

// ── Events ───────────────────────────────────────────────────────────────

static EventType parse_event_type(const std::string& s) {
    std::string lower = s;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower == "rain") return EventType::Rain;
    if (lower == "poweroutage" || lower == "power_outage")
        return EventType::PowerOutage;
    if (lower == "vip" || lower == "vipvisit" || lower == "vip_visit")
        return EventType::VIPVisit;
    if (lower == "heatwave" || lower == "heat_wave" || lower == "heat")
        return EventType::HeatWave;
    return EventType::Rain;
}

static const char* event_type_name(EventType t) {
    switch (t) {
        case EventType::Rain:
            return "rain";
        case EventType::PowerOutage:
            return "poweroutage";
        case EventType::VIPVisit:
            return "vipvisit";
        case EventType::HeatWave:
            return "heatwave";
    }
    return "unknown";
}

static void cmd_trigger_event(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("trigger_event requires TYPE DURATION_SECONDS");
        return;
    }
    EventType type = parse_event_type(cmd.arg(0));
    float duration = cmd.arg_as<float>(1);
    Entity& ev_entity = EntityHelper::createEntity();
    ev_entity.addComponent<ActiveEvent>();
    auto& ev = ev_entity.get<ActiveEvent>();
    ev.type = type;
    ev.duration = duration;
    ev.description = event_type_name(type);
    EntityHelper::merge_entity_arrays();
    spawn_toast("Event: " + ev.description + "!");
    log_info("[E2E] trigger_event: {} for {:.1f}s", ev.description, duration);
    cmd.consume();
}

static void cmd_assert_event_active(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("assert_event_active requires TYPE");
        return;
    }
    EventType expected = parse_event_type(cmd.arg(0));
    auto events = EntityQuery().whereHasComponent<ActiveEvent>().gen();
    bool found = false;
    for (Entity& ev : events) {
        if (ev.get<ActiveEvent>().type == expected) {
            found = true;
            break;
        }
    }
    if (!found)
        cmd.fail(fmt::format("assert_event_active FAILED: no active {} event",
                             event_type_name(expected)));
    else {
        log_info("assert_event_active PASSED: {}", event_type_name(expected));
        cmd.consume();
    }
}

static void cmd_assert_event_inactive(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("assert_event_inactive requires TYPE");
        return;
    }
    EventType expected = parse_event_type(cmd.arg(0));
    auto events = EntityQuery().whereHasComponent<ActiveEvent>().gen();
    for (Entity& ev : events) {
        if (ev.get<ActiveEvent>().type == expected) {
            cmd.fail(
                fmt::format("assert_event_inactive FAILED: {} is still active",
                            event_type_name(expected)));
            return;
        }
    }
    log_info("assert_event_inactive PASSED: {}", event_type_name(expected));
    cmd.consume();
}

// ── Difficulty ───────────────────────────────────────────────────────────

static void cmd_assert_day_number(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_day_number requires OP VALUE");
        return;
    }
    auto* diff = EntityHelper::get_singleton_cmp<DifficultyState>();
    int actual = diff ? diff->day_number : 0;
    if (!compare_op(actual, cmd.arg(0), cmd.arg_as<int>(1)))
        cmd.fail(fmt::format("assert_day_number failed: {} {} {} (actual: {})",
                             actual, cmd.arg(0), cmd.arg_as<int>(1), actual));
    else {
        log_info("assert_day_number PASSED: {}", actual);
        cmd.consume();
    }
}

static void cmd_set_day_number(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("set_day_number requires VALUE");
        return;
    }
    auto* diff = EntityHelper::get_singleton_cmp<DifficultyState>();
    if (diff) {
        diff->day_number = cmd.arg_as<int>(0);
        diff->spawn_rate_mult = 1.0f + (diff->day_number - 1) * 0.15f;
        diff->crowd_size_mult = 1.0f + (diff->day_number - 1) * 0.1f;
        log_info("[E2E] set_day_number: {} (spawn_mult={:.2f})",
                 diff->day_number, diff->spawn_rate_mult);
    }
    cmd.consume();
}

static void cmd_assert_spawn_rate(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_spawn_rate requires OP VALUE");
        return;
    }
    auto* diff = EntityHelper::get_singleton_cmp<DifficultyState>();
    float actual = diff ? diff->spawn_rate_mult : 0.f;
    if (!compare_op_f(actual, cmd.arg(0), cmd.arg_as<float>(1)))
        cmd.fail(
            fmt::format("assert_spawn_rate failed: {:.2f} {} {:.2f} (actual: "
                        "{:.2f})",
                        actual, cmd.arg(0), cmd.arg_as<float>(1), actual));
    else {
        log_info("assert_spawn_rate PASSED: {:.2f}", actual);
        cmd.consume();
    }
}

// ── Toasts ───────────────────────────────────────────────────────────────

static void cmd_assert_toast_contains(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("assert_toast_contains requires TEXT");
        return;
    }
    std::string needle = cmd.arg(0);
    std::string needle_lower = needle;
    std::transform(needle_lower.begin(), needle_lower.end(),
                   needle_lower.begin(), ::tolower);
    auto toasts = EntityQuery().whereHasComponent<ToastMessage>().gen();
    for (Entity& t : toasts) {
        std::string text = t.get<ToastMessage>().text;
        std::string text_lower = text;
        std::transform(text_lower.begin(), text_lower.end(), text_lower.begin(),
                       ::tolower);
        if (text_lower.find(needle_lower) != std::string::npos) {
            log_info("assert_toast_contains PASSED: found '{}' in '{}'", needle,
                     text);
            cmd.consume();
            return;
        }
    }
    cmd.fail(fmt::format(
        "assert_toast_contains FAILED: no toast contains '{}' ({} active)",
        needle, toasts.size()));
}

static void cmd_assert_no_toast(testing::PendingE2ECommand& cmd) {
    auto toasts = EntityQuery().whereHasComponent<ToastMessage>().gen();
    if (!toasts.empty())
        cmd.fail(fmt::format("assert_no_toast FAILED: {} toasts active",
                             toasts.size()));
    else {
        log_info("assert_no_toast PASSED");
        cmd.consume();
    }
}

// ── Save/Load ────────────────────────────────────────────────────────────

static void cmd_save_game(testing::PendingE2ECommand& cmd) {
    if (save::save_game())
        log_info("[E2E] save_game: saved successfully");
    else
        log_warn("[E2E] save_game: FAILED to save");
    cmd.consume();
}

static void cmd_load_game(testing::PendingE2ECommand& cmd) {
    if (save::load_game())
        log_info("[E2E] load_game: loaded successfully");
    else
        log_warn("[E2E] load_game: FAILED to load");
    cmd.consume();
}

static void cmd_assert_save_exists(testing::PendingE2ECommand& cmd) {
    if (save::has_save_file()) {
        log_info("assert_save_exists PASSED");
        cmd.consume();
    } else {
        cmd.fail("assert_save_exists FAILED: no save file");
    }
}

static void cmd_delete_save(testing::PendingE2ECommand& cmd) {
    save::delete_save();
    log_info("[E2E] delete_save: done");
    cmd.consume();
}

// ── Registration ─────────────────────────────────────────────────────────

static void init_e2e_registry() {
    auto& r = get_registry();
    if (!r.handlers.empty()) return;
    r.add("spawn_agent", cmd_spawn_agent);
    r.add("spawn_agents", cmd_spawn_agents);
    r.add("clear_agents", cmd_clear_agents);
    r.add("clear_map", cmd_clear_map);
    r.add("reset_game", cmd_reset_game);
    r.add("place_facility", cmd_place_facility);
    r.add("set_tile", cmd_set_tile);
    r.add("get_agent_count", cmd_get_agent_count);
    r.add("get_density", cmd_get_density);
    r.add("assert_agent_count", cmd_assert_agent_count);
    r.add("assert_density", cmd_assert_density);
    r.add("assert_tile_type", cmd_assert_tile_type);
    r.add("draw_path_rect", cmd_draw_path_rect);
    r.add("move_to_grid", cmd_move_to_grid);
    r.add("click_grid", cmd_click_grid);
    r.add("assert_agent_near", cmd_assert_agent_near);
    r.add("set_spawn_rate", cmd_set_spawn_rate);
    r.add("set_spawn_enabled", cmd_set_spawn_enabled);
    r.add("force_need", cmd_force_need);
    r.add("assert_agents_at_facility", cmd_assert_agents_at_facility);
    r.add("assert_agent_watching", cmd_assert_agent_watching);
    r.add("assert_agents_on_tiletype", cmd_assert_agents_on_tiletype);
    r.add("place_gate", cmd_place_gate);
    r.add("assert_gate_count", cmd_assert_gate_count);
    r.add("assert_blocked", cmd_assert_blocked);
    r.add("toggle_debug", cmd_toggle_debug);
    r.add("trigger_game_over", cmd_trigger_game_over);
    r.add("assert_game_status", cmd_assert_game_status);
    r.add("toggle_overlay", cmd_toggle_overlay);
    r.add("assert_overlay", cmd_assert_overlay);
    r.add("set_agent_speed", cmd_set_agent_speed);
    r.add("set_move_speed", cmd_set_move_speed);
    r.add("get_death_count", cmd_get_death_count);
    r.add("set_death_count", cmd_set_death_count);
    r.add("assert_death_count", cmd_assert_death_count);
    r.add("assert_agent_hp", cmd_assert_agent_hp);
    r.add("set_time", cmd_set_time);
    r.add("set_speed", cmd_set_speed);
    r.add("assert_phase", cmd_assert_phase);
    r.add("assert_time_between", cmd_assert_time_between);
    r.add("assert_stage_state", cmd_assert_stage_state);
    r.add("force_artist", cmd_force_artist);
    r.add("assert_agents_exited", cmd_assert_agents_exited);
    r.add("assert_carryover_count", cmd_assert_carryover_count);
    r.add("set_pheromone", cmd_set_pheromone);
    r.add("assert_pheromone", cmd_assert_pheromone);
    r.add("clear_pheromones", cmd_clear_pheromones);
    r.add("set_max_attendees", cmd_set_max_attendees);
    r.add("assert_max_attendees", cmd_assert_max_attendees);
    r.add("assert_slots", cmd_assert_slots);
    r.add("select_tool", cmd_select_tool);
    r.add("assert_tool", cmd_assert_tool);
    r.add("place_building", cmd_place_building);
    r.add("demolish_at", cmd_demolish_at);
    r.add("set_all_agent_hp", cmd_set_all_agent_hp);
    r.add("perf_start", cmd_perf_start);
    r.add("perf_report", cmd_perf_report);
    r.add("assert_fps", cmd_assert_fps);
    r.add("assert_min_fps", cmd_assert_min_fps);
    r.add("assert_nux_active", cmd_assert_nux_active);
    r.add("assert_nux_inactive", cmd_assert_nux_inactive);
    r.add("assert_nux_count", cmd_assert_nux_count);
    r.add("dismiss_nux", cmd_dismiss_nux);
    r.add("assert_pixel", cmd_assert_pixel);
    r.add("assert_region_not_blank", cmd_assert_region_not_blank);
    r.add("set_zoom", cmd_set_zoom);
    r.add("trigger_event", cmd_trigger_event);
    r.add("assert_event_active", cmd_assert_event_active);
    r.add("assert_event_inactive", cmd_assert_event_inactive);
    r.add("assert_day_number", cmd_assert_day_number);
    r.add("set_day_number", cmd_set_day_number);
    r.add("assert_spawn_rate", cmd_assert_spawn_rate);
    r.add("assert_toast_contains", cmd_assert_toast_contains);
    r.add("assert_no_toast", cmd_assert_no_toast);
    r.add("save_game", cmd_save_game);
    r.add("load_game", cmd_load_game);
    r.add("assert_save_exists", cmd_assert_save_exists);
    r.add("delete_save", cmd_delete_save);
}

void register_e2e_systems(SystemManager& sm) {
    testing::register_builtin_handlers(sm);
    init_e2e_registry();
    sm.register_update_system(std::make_unique<E2EDispatchSystem>());
    testing::register_unknown_handler(sm);
    testing::register_cleanup(sm);
}
