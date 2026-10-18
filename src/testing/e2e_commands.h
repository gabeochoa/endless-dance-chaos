#pragma once

// Custom E2E command handlers for game-specific test commands.
// Uses a dispatch table instead of individual System structs to reduce
// boilerplate (~40 commands, each needs the same guard pattern).

#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include <chrono>

#include "components.h"
#include "entity_makers.h"
#include "game.h"
#include "guest_thoughts.h"
#include "render_helpers.h"
#include "save_system.h"
#include "systems.h"
#include "update_helpers.h"

#include "afterhours/src/core/entity_helper.h"
#include "afterhours/src/core/entity_query.h"
#include "afterhours/src/plugins/e2e_testing/e2e_testing.h"
#include "afterhours/src/plugins/e2e_testing/perf_commands.h"

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
    if (lower == "path" || lower == "concrete" || lower == "concretepath" ||
        lower == "concrete_path")
        return TileType::Path;
    if (lower == "dirt" || lower == "dirtpath" || lower == "dirt_path")
        return TileType::DirtPath;
    if (lower == "neon" || lower == "neonpath" || lower == "neon_path")
        return TileType::NeonPath;
    if (lower == "lamp") return TileType::Lamp;
    if (lower == "fence") return TileType::Fence;
    if (lower == "gate") return TileType::Gate;
    if (lower == "stage") return TileType::Stage;
    if (lower == "stagefloor" || lower == "stage_floor")
        return TileType::StageFloor;
    if (lower == "bathroom") return TileType::Bathroom;
    if (lower == "food") return TileType::Food;
    if (lower == "medtent" || lower == "med_tent" || lower == "med")
        return TileType::MedTent;
    if (lower == "bar") return TileType::Bar;
    if (lower == "securitypost" || lower == "security_post" ||
        lower == "security" || lower == "sec")
        return TileType::SecurityPost;
    if (lower == "waterstation" || lower == "water_station" || lower == "water")
        return TileType::WaterStation;
    if (lower == "locked") return TileType::Locked;
    if (lower == "tree") return TileType::Tree;
    if (lower == "bench") return TileType::Bench;
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
    if (lower == "bar") return FacilityType::Bar;
    if (lower == "waterstation" || lower == "water_station" || lower == "water")
        return FacilityType::WaterStation;
    return FacilityType::Bathroom;
}

inline std::optional<vec2> grid_to_screen(int gx, int gz) {
    auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
    if (!cam) return std::nullopt;
    // world_to_screen branches on the view mode, so click_grid /
    // move_to_grid land on the same tile the player sees in iso and in
    // top-down mode. This is the inverse of screen_to_grid: a round-trip
    // through these two functions must return the starting tile.
    vec3 world_pos = {gx * TILESIZE, 0.0f, gz * TILESIZE};
    return cam->cam.world_to_screen(world_pos);
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
        // Only the bought (playable) rectangle clears to grass: the
        // fence ring and Locked land outside it survive.
        for (int z = 0; z < MAP_SIZE; z++) {
            for (int x = 0; x < MAP_SIZE; x++) {
                if (!grid->in_playable(x, z)) continue;
                grid->at(x, z).type = TileType::Grass;
                grid->at(x, z).height = 0;
                grid->at(x, z).agent_count = 0;
            }
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
        cmd.fail("draw_path_rect requires X1 Z1 X2 Z2 [TYPE]");
        return;
    }
    int x1 = cmd.arg_as<int>(0), z1 = cmd.arg_as<int>(1);
    int x2 = cmd.arg_as<int>(2), z2 = cmd.arg_as<int>(3);
    // Free staging helper (like set_tile): no economy. Charging
    // placement is place_path_rect. Default type is concrete (Path).
    TileType fill = TileType::Path;
    if (cmd.has_args(5)) {
        fill = parse_tile_type(cmd.arg(4));
        if (!is_path_tile(fill)) {
            cmd.fail("draw_path_rect: TYPE must be a path type");
            return;
        }
    }
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
                grid->at(x, z).type = fill;
    grid->mark_tiles_dirty();
    cmd.consume();
}

// place_path_rect X1 Z1 X2 Z2 TYPE — charging path placement, same
// rule as the real drag tool: only Grass tiles convert, cost is
// per-tile from PATH_TYPE_INFOS, whole rect refused if unaffordable.
static void cmd_place_path_rect(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(5)) {
        cmd.fail("place_path_rect requires X1 Z1 X2 Z2 TYPE");
        return;
    }
    int x1 = cmd.arg_as<int>(0), z1 = cmd.arg_as<int>(1);
    int x2 = cmd.arg_as<int>(2), z2 = cmd.arg_as<int>(3);
    TileType fill = parse_tile_type(cmd.arg(4));
    const PathTypeInfo* info = path_info_for(fill);
    if (!info) {
        cmd.fail("place_path_rect: TYPE must be a path type");
        return;
    }
    auto* grid = EntityHelper::get_singleton_cmp<Grid>();
    if (!grid) {
        cmd.fail("place_path_rect: no grid");
        return;
    }
    int min_x = std::min(x1, x2), min_z = std::min(z1, z2);
    int max_x = std::max(x1, x2), max_z = std::max(z1, z2);
    int converts = 0;
    for (int z = min_z; z <= max_z; z++)
        for (int x = min_x; x <= max_x; x++)
            if (grid->in_bounds(x, z) && grid->at(x, z).type == TileType::Grass)
                converts++;
    int cost = converts * info->cost_per_tile;
    auto* econ = EntityHelper::get_singleton_cmp<Economy>();
    if (econ && !econ->try_spend(cost)) {
        spawn_toast(fmt::format("Not enough cash! {} path costs ${}",
                                info->name, cost));
        log_warn(
            "[E2E] place_path_rect refused: {} path costs ${}, cash "
            "is ${}",
            info->name, cost, econ->cash);
        cmd.consume();
        return;
    }
    for (int z = min_z; z <= max_z; z++)
        for (int x = min_x; x <= max_x; x++)
            if (grid->in_bounds(x, z) && grid->at(x, z).type == TileType::Grass)
                grid->at(x, z).type = fill;
    grid->mark_tiles_dirty();
    cmd.consume();
}

// assert_tile_height X Z OP VALUE — terrain height step of one tile.
static void cmd_assert_tile_height(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(4)) {
        cmd.fail("assert_tile_height requires X Z OP VALUE");
        return;
    }
    int x = cmd.arg_as<int>(0), z = cmd.arg_as<int>(1);
    auto* grid = EntityHelper::get_singleton_cmp<Grid>();
    if (!grid || !grid->in_bounds(x, z)) {
        cmd.fail("assert_tile_height: out of bounds");
        return;
    }
    int actual = grid->at(x, z).height;
    if (!compare_op(actual, cmd.arg(2), cmd.arg_as<int>(3)))
        cmd.fail(fmt::format(
            "assert_tile_height at ({},{}) failed: {} {} {} (actual: {})", x, z,
            actual, cmd.arg(2), cmd.arg_as<int>(3), actual));
    else
        cmd.consume();
}

// assert_lamp_glow X Z — the tile is a Lamp and night glow is active,
// i.e. the lamp's glow pool is drawing (night_t > 0.5).
static void cmd_assert_lamp_glow(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_lamp_glow requires X Z");
        return;
    }
    int x = cmd.arg_as<int>(0), z = cmd.arg_as<int>(1);
    auto* grid = EntityHelper::get_singleton_cmp<Grid>();
    if (!grid || !grid->in_bounds(x, z)) {
        cmd.fail("assert_lamp_glow: out of bounds");
        return;
    }
    bool is_lamp = grid->at(x, z).type == TileType::Lamp;
    bool glow = night_glow_active();
    if (!is_lamp || !glow)
        cmd.fail(
            fmt::format("assert_lamp_glow at ({},{}) FAILED: lamp={}, glow={}",
                        x, z, is_lamp ? "yes" : "no", glow ? "on" : "off"));
    else {
        log_info("assert_lamp_glow PASSED: ({},{})", x, z);
        cmd.consume();
    }
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
    auto agents = EntityQuery().whereHasComponent<Agent, Transform>().gen();
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
    auto agents = EntityQuery().whereHasComponent<Agent, AgentNeeds>().gen();
    for (Entity& a : agents) {
        auto& needs = a.get<AgentNeeds>();
        if (type_str == "bathroom")
            needs.needs_bathroom = true;
        else if (type_str == "food")
            needs.needs_food = true;
        else if (type_str == "bar")
            needs.needs_bar = true;
        else if (type_str == "heat") {
            // Hot enough to seek relief, ~2s from collapse.
            needs.needs_heat = true;
            if (needs.heat_threshold <= 0.f) needs.heat_threshold = 90.f;
            needs.heat_timer = needs.heat_threshold - 2.f;
        }
        // A forced need interrupts watching, queueing, and bench rest
        // alike: the agent leaves the stage for the facility.
        if (!a.is_missing<WatchingStage>()) a.removeComponent<WatchingStage>();
        if (!a.is_missing<QueuedForStage>())
            a.removeComponent<QueuedForStage>();
        if (!a.is_missing<RestingOnBench>())
            a.removeComponent<RestingOnBench>();
        if (!a.is_missing<Agent>()) a.get<Agent>().queue_admitted = false;
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
    auto agents = EntityQuery().whereHasComponent<Agent, BeingServiced>().gen();
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
    auto agents = EntityQuery().whereHasComponent<Agent, Transform>().gen();
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
    auto agents =
        EntityQuery().whereHasComponent<Agent, Transform, AgentHealth>().gen();
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

// ── Atmosphere (day/night + weather presentation) ─────────────────────
// All values come from render_helpers.h so E2E asserts the same palette
// math the renderers draw: brightness, tint, streak count, night glow.

// get_brightness — log the current scene brightness in [0, 1].
static void cmd_get_brightness(testing::PendingE2ECommand& cmd) {
    log_info("[E2E] Brightness: {:.3f}", get_brightness());
    cmd.consume();
}

// assert_brightness OP VALUE — compare scene brightness (float).
static void cmd_assert_brightness(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_brightness requires OP VALUE");
        return;
    }
    float actual = get_brightness();
    if (!compare_op_f(actual, cmd.arg(0), cmd.arg_as<float>(1)))
        cmd.fail(fmt::format("assert_brightness failed: {:.3f} {} {:.3f}",
                             actual, cmd.arg(0), cmd.arg_as<float>(1)));
    else {
        log_info("assert_brightness PASSED: {:.3f} {} {:.3f}", actual,
                 cmd.arg(0), cmd.arg_as<float>(1));
        cmd.consume();
    }
}

// assert_rain_active / assert_rain_inactive — Rain ActiveEvent present.
static void cmd_assert_rain_active(testing::PendingE2ECommand& cmd) {
    if (!atmosphere_weather().rain) {
        cmd.fail("assert_rain_active FAILED: no active Rain event");
        return;
    }
    log_info("assert_rain_active PASSED");
    cmd.consume();
}

static void cmd_assert_rain_inactive(testing::PendingE2ECommand& cmd) {
    if (atmosphere_weather().rain) {
        cmd.fail("assert_rain_inactive FAILED: Rain event is still active");
        return;
    }
    log_info("assert_rain_inactive PASSED");
    cmd.consume();
}

// get_rain_streak_count — log the drawn rain streak count (120 or 0).
static void cmd_get_rain_streak_count(testing::PendingE2ECommand& cmd) {
    log_info("[E2E] Rain streak count: {}", rain_streak_count());
    cmd.consume();
}

// assert_rain_streak_count OP VALUE
static void cmd_assert_rain_streak_count(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_rain_streak_count requires OP VALUE");
        return;
    }
    int actual = rain_streak_count();
    if (!compare_op(actual, cmd.arg(0), cmd.arg_as<int>(1)))
        cmd.fail(fmt::format("assert_rain_streak_count failed: {} {} {}",
                             actual, cmd.arg(0), cmd.arg_as<int>(1)));
    else {
        log_info("assert_rain_streak_count PASSED: {}", actual);
        cmd.consume();
    }
}

// assert_weather_tint warm|cool|neutral — current palette tint.
static void cmd_assert_weather_tint(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("assert_weather_tint requires TINT");
        return;
    }
    std::string expected = cmd.arg(0);
    std::transform(expected.begin(), expected.end(), expected.begin(),
                   ::tolower);
    std::string actual = weather_tint_name();
    if (actual != expected)
        cmd.fail(
            fmt::format("assert_weather_tint FAILED: expected '{}', got '{}'",
                        expected, actual));
    else {
        log_info("assert_weather_tint PASSED: {}", actual);
        cmd.consume();
    }
}

// assert_night_glow on|off — stage cones + lamp pools are drawing.
static void cmd_assert_night_glow(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("assert_night_glow requires on|off");
        return;
    }
    std::string expected = cmd.arg(0);
    std::transform(expected.begin(), expected.end(), expected.begin(),
                   ::tolower);
    bool active = night_glow_active();
    bool match = (expected == "on" && active) || (expected == "off" && !active);
    if (!match)
        cmd.fail(fmt::format("assert_night_glow FAILED: expected '{}', glow {}",
                             expected, active ? "on" : "off"));
    else {
        log_info("assert_night_glow PASSED: {}", expected);
        cmd.consume();
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
    if (tool == "path" || tool == "concrete" || tool == "concretepath")
        bs->tool = BuildTool::Path;
    else if (tool == "dirtpath" || tool == "dirt")
        bs->tool = BuildTool::DirtPath;
    else if (tool == "neonpath" || tool == "neon")
        bs->tool = BuildTool::NeonPath;
    else if (tool == "lamp")
        bs->tool = BuildTool::Lamp;
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
    else if (tool == "bar")
        bs->tool = BuildTool::Bar;
    else if (tool == "securitypost" || tool == "security" || tool == "sec")
        bs->tool = BuildTool::SecurityPost;
    else if (tool == "waterstation" || tool == "water")
        bs->tool = BuildTool::WaterStation;
    else if (tool == "tree")
        bs->tool = BuildTool::Tree;
    else if (tool == "bench")
        bs->tool = BuildTool::Bench;
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
    static const char* names[] = {
        "path",     "fence",        "gate",         "stage",
        "bathroom", "food",         "medtent",      "demolish",
        "bar",      "securitypost", "waterstation", "tree",
        "bench",    "dirtpath",     "neonpath",     "lamp"};
    int idx = static_cast<int>(bs->tool);
    std::string actual = (idx >= 0 && idx < 16) ? names[idx] : "unknown";
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
    TileType tile_type = TileType::Grass;
    int w = 0, h = 0;
    if (type == "gate") {
        tile_type = TileType::Gate;
        w = 1;
        h = 2;
    } else if (type == "stage") {
        tile_type = TileType::Stage;
        w = 4;
        h = 4;
    } else if (type == "bathroom") {
        tile_type = TileType::Bathroom;
        w = 2;
        h = 2;
    } else if (type == "food") {
        tile_type = TileType::Food;
        w = 2;
        h = 2;
    } else if (type == "medtent" || type == "med") {
        tile_type = TileType::MedTent;
        w = 2;
        h = 2;
    } else if (type == "bar") {
        tile_type = TileType::Bar;
        w = 2;
        h = 2;
    } else if (type == "securitypost" || type == "security" || type == "sec") {
        tile_type = TileType::SecurityPost;
        w = 1;
        h = 1;
    } else if (type == "waterstation" || type == "water") {
        tile_type = TileType::WaterStation;
        w = 1;
        h = 1;
    } else if (type == "tree") {
        tile_type = TileType::Tree;
        w = 1;
        h = 1;
    } else if (type == "bench") {
        tile_type = TileType::Bench;
        w = 1;
        h = 1;
    } else if (type == "lamp") {
        tile_type = TileType::Lamp;
        w = 1;
        h = 1;
    }
    if (w == 0) {
        cmd.consume();
        return;
    }
    // Same placement rule as the real build path (PathBuildSystem):
    // every footprint tile must be inside the bought park and on
    // grass / path / stage floor. Locked land is unbuildable, and a
    // refused placement charges nothing.
    bool placeable = true;
    for (int dz = 0; dz < h && placeable; dz++) {
        for (int dx = 0; dx < w; dx++) {
            int tx = x + dx, tz = z + dz;
            if (!grid->in_playable(tx, tz)) {
                placeable = false;
                break;
            }
            TileType t = grid->at(tx, tz).type;
            if (t != TileType::Grass && !is_path_tile(t) &&
                t != TileType::StageFloor) {
                placeable = false;
                break;
            }
        }
    }
    if (!placeable) {
        log_info("[E2E] place_building refused: ({},{}) not buildable", x, z);
        cmd.consume();
        return;
    }
    // Same cost/refusal rule as the real build path: this helper is the
    // E2E building abstraction, so it must not bypass the economy.
    auto* econ = EntityHelper::get_singleton_cmp<Economy>();
    int cost = build_cost_for_tile(tile_type);
    if (econ && !econ->try_spend(cost)) {
        spawn_toast(fmt::format("Not enough cash! {} costs ${}", type, cost));
        log_warn("[E2E] place_building refused: {} costs ${}, cash is ${}",
                 type, cost, econ->cash);
        cmd.consume();
        return;
    }
    grid->place_footprint(x, z, w, h, tile_type);
    cmd.consume();
}

// set_cash VALUE — debug setter for the economy (does not touch totals)
static void cmd_set_cash(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("set_cash requires VALUE");
        return;
    }
    auto* econ = EntityHelper::get_singleton_cmp<Economy>();
    if (!econ) {
        cmd.fail("set_cash: no Economy");
        return;
    }
    econ->cash = cmd.arg_as<int>(0);
    log_info("[E2E] set_cash: {}", econ->cash);
    cmd.consume();
}

static void cmd_assert_cash(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_cash requires OP VALUE");
        return;
    }
    auto* econ = EntityHelper::get_singleton_cmp<Economy>();
    int actual = econ ? econ->cash : 0;
    if (!compare_op(actual, cmd.arg(0), cmd.arg_as<int>(1)))
        cmd.fail(fmt::format("assert_cash failed: {} {} {} (actual: {})",
                             actual, cmd.arg(0), cmd.arg_as<int>(1), actual));
    else
        cmd.consume();
}

static void cmd_assert_total_earned(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_total_earned requires OP VALUE");
        return;
    }
    auto* econ = EntityHelper::get_singleton_cmp<Economy>();
    int actual = econ ? econ->total_earned : 0;
    if (!compare_op(actual, cmd.arg(0), cmd.arg_as<int>(1)))
        cmd.fail(
            fmt::format("assert_total_earned failed: {} {} {} (actual: {})",
                        actual, cmd.arg(0), cmd.arg_as<int>(1), actual));
    else
        cmd.consume();
}

static void cmd_assert_total_spent(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_total_spent requires OP VALUE");
        return;
    }
    auto* econ = EntityHelper::get_singleton_cmp<Economy>();
    int actual = econ ? econ->total_spent : 0;
    if (!compare_op(actual, cmd.arg(0), cmd.arg_as<int>(1)))
        cmd.fail(fmt::format("assert_total_spent failed: {} {} {} (actual: {})",
                             actual, cmd.arg(0), cmd.arg_as<int>(1), actual));
    else
        cmd.consume();
}

// ── Loans + finance history ──────────────────────────────────────────────

// borrow_loan AMOUNT — same path as the Finances tab button. Consumed
// even when refused (bad amount / debt cap), so the test can assert
// the refusal toast and unchanged cash/debt.
static void cmd_borrow_loan(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("borrow_loan requires AMOUNT");
        return;
    }
    borrow_loan(cmd.arg_as<int>(0));
    cmd.consume();
}

// repay_loan AMOUNT|all — same path as the Finances tab buttons.
// Consumed even when refused (no debt / unaffordable).
static void cmd_repay_loan(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("repay_loan requires AMOUNT|all");
        return;
    }
    std::string arg = cmd.arg(0);
    std::transform(arg.begin(), arg.end(), arg.begin(), ::tolower);
    if (arg == "all")
        repay_loan_all();
    else
        repay_loan(cmd.arg_as<int>(0));
    cmd.consume();
}

static void cmd_assert_debt(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_debt requires OP VALUE");
        return;
    }
    auto* econ = EntityHelper::get_singleton_cmp<Economy>();
    int actual = econ ? econ->debt : 0;
    if (!compare_op(actual, cmd.arg(0), cmd.arg_as<int>(1)))
        cmd.fail(fmt::format("assert_debt failed: {} {} {} (actual: {})",
                             actual, cmd.arg(0), cmd.arg_as<int>(1), actual));
    else
        cmd.consume();
}

static void cmd_assert_history_count(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_history_count requires OP VALUE");
        return;
    }
    auto* econ = EntityHelper::get_singleton_cmp<Economy>();
    int actual = econ ? static_cast<int>(econ->finance_history.size()) : 0;
    if (!compare_op(actual, cmd.arg(0), cmd.arg_as<int>(1)))
        cmd.fail(
            fmt::format("assert_history_count failed: {} {} {} (actual: {})",
                        actual, cmd.arg(0), cmd.arg_as<int>(1), actual));
    else
        cmd.consume();
}

// assert_history_day DAY INCOME EXPENSES — the closed day's row must
// exist with exactly these sums (profit is income - expenses).
static void cmd_assert_history_day(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(3)) {
        cmd.fail("assert_history_day requires DAY INCOME EXPENSES");
        return;
    }
    int day = cmd.arg_as<int>(0);
    int want_income = cmd.arg_as<int>(1);
    int want_expenses = cmd.arg_as<int>(2);
    auto* econ = EntityHelper::get_singleton_cmp<Economy>();
    if (econ) {
        for (const auto& row : econ->finance_history) {
            if (row.day != day) continue;
            if (row.income == want_income && row.expenses == want_expenses) {
                cmd.consume();
                return;
            }
            cmd.fail(fmt::format(
                "assert_history_day failed: Day {} is income ${} expenses "
                "${}, wanted ${} / ${}",
                day, row.income, row.expenses, want_income, want_expenses));
            return;
        }
    }
    cmd.fail(fmt::format("assert_history_day failed: no Day {} row", day));
}

// charge_wages — deduct the day's wages without the payroll quit-out, so
// the cash-crisis guard (floor at 0 + rating hit + ticker) can be
// exercised with cash driven negative via wages.
static void cmd_charge_wages(testing::PendingE2ECommand& cmd) {
    charge_wages_unchecked();
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
    auto agents = EntityQuery().whereHasComponent<Agent, AgentHealth>().gen();
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

    // Also time every registered system, so `dump_profile` can say which one
    // ate the frame instead of only that the frame was slow. Only while a perf
    // script asked for it. builtin_profile::enable() installs the whole
    // provider (FPS, p99, top entries) from afterhours' Collector, so there
    // is no app provider to set up first.
    afterhours::testing::perf_commands::builtin_profile::enable();

    log_info("[E2E] perf_start: sampling FPS and per-system time every frame");
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

// assert_profile_sampled — fail unless dump_profile's provider has at least
// one system entry with a real timing sample (calls > 0). Catches the bump
// failure mode where profiling is compiled out (no entries at all) or the app
// never calls Collector::end_frame() (entries exist, all with 0 calls), both
// of which made dump_profile print "Top 0 systems" while still passing.
static void cmd_assert_profile_sampled(testing::PendingE2ECommand& cmd) {
    namespace perf = afterhours::testing::perf_commands;
    auto& p = perf::provider();
    if (!p.top_entries) {
        cmd.fail(
            "assert_profile_sampled FAILED: no perf provider "
            "configured (run perf_start first)");
        return;
    }
    auto entries = p.top_entries(20);
    for (const auto& e : entries) {
        if (e.calls && *e.calls > 0) {
            log_info("assert_profile_sampled PASSED: '{}' has {} calls", e.name,
                     *e.calls);
            cmd.consume();
            return;
        }
    }
    cmd.fail(fmt::format(
        "assert_profile_sampled FAILED: {} profile entries, none with any "
        "calls (profiling compiled out or end_frame() never called)",
        entries.size()));
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

static void cmd_assert_zoom(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_zoom requires OP VALUE");
        return;
    }
    auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
    if (!cam) {
        cmd.fail("assert_zoom: no camera");
        return;
    }
    float actual = cam->cam.distance;
    std::string op = cmd.args[0];
    float expected = cmd.arg_as<float>(1);
    if (compare_op_f(actual, op, expected))
        cmd.consume();
    else
        cmd.fail(fmt::format("assert_zoom failed: {} {} {} (actual {})", actual,
                             op, expected, actual));
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
    if (lower == "viraloutbreak" || lower == "viral_outbreak" ||
        lower == "viral" || lower == "outbreak")
        return EventType::ViralOutbreak;
    if (lower == "surpriseinspection" || lower == "surprise_inspection" ||
        lower == "surprise" || lower == "inspection")
        return EventType::SurpriseInspection;
    if (lower == "generoussponsor" || lower == "generous_sponsor" ||
        lower == "sponsor")
        return EventType::GenerousSponsor;
    if (lower == "equipmentfailure" || lower == "equipment_failure" ||
        lower == "equipment" || lower == "failure")
        return EventType::EquipmentFailure;
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
        case EventType::ViralOutbreak:
            return "viraloutbreak";
        case EventType::SurpriseInspection:
            return "surpriseinspection";
        case EventType::GenerousSponsor:
            return "generoussponsor";
        case EventType::EquipmentFailure:
            return "equipmentfailure";
    }
    return "unknown";
}

// trigger_event TYPE DURATION_SECONDS [OFFLINE_FACILITY]
// Same path as the director's fire_event (toast + ticker + instant
// effects). The optional 3rd arg pins an Equipment Failure's offline
// facility; without it the pick is random. Instant events ignore the
// duration. Forced events never touch the director's daily quota or
// no-repeat bookkeeping.
static void cmd_trigger_event(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("trigger_event requires TYPE DURATION_SECONDS");
        return;
    }
    EventType type = parse_event_type(cmd.arg(0));
    float duration = cmd.arg_as<float>(1);
    int offline = -1;
    if (cmd.has_args(3)) {
        FacilityType ft = parse_facility_type(cmd.arg(2));
        offline = static_cast<int>(ft);
    }
    fire_event(type, duration, offline);
    log_info("[E2E] trigger_event: {} for {:.1f}s", event_type_name(type),
             duration);
    cmd.consume();
}

// set_event_seed SEED — reseed the director's dedicated RNG.
static void cmd_set_event_seed(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("set_event_seed requires SEED");
        return;
    }
    set_event_seed(cmd.arg(0));
    log_info("[E2E] set_event_seed: {}", cmd.arg(0));
    cmd.consume();
}

// roll_events — fire the current day's remaining director quota now.
static void cmd_roll_events(testing::PendingE2ECommand& cmd) {
    int fired = director_roll_today();
    log_info("[E2E] roll_events: fired {}", fired);
    cmd.consume();
}

// assert_director_no_repeat — today's director picks share no type with
// yesterday's and contain no duplicates within today.
static void cmd_assert_director_no_repeat(testing::PendingE2ECommand& cmd) {
    auto* director = EntityHelper::get_singleton_cmp<EventDirectorState>();
    if (!director) {
        cmd.fail("assert_director_no_repeat: no EventDirectorState");
        return;
    }
    std::vector<int> seen = director->fired_today;
    std::sort(seen.begin(), seen.end());
    for (size_t i = 1; i < seen.size(); i++) {
        if (seen[i] == seen[i - 1]) {
            cmd.fail(fmt::format(
                "assert_director_no_repeat FAILED: type {} fired twice today",
                seen[i]));
            return;
        }
    }
    for (int v : director->fired_today) {
        if (std::find(director->fired_yesterday.begin(),
                      director->fired_yesterday.end(),
                      v) != director->fired_yesterday.end()) {
            cmd.fail(fmt::format(
                "assert_director_no_repeat FAILED: type {} fired on "
                "consecutive days",
                v));
            return;
        }
    }
    log_info("assert_director_no_repeat PASSED");
    cmd.consume();
}

// assert_event_fired_today TYPE — the director fired TYPE today.
static void cmd_assert_event_fired_today(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("assert_event_fired_today requires TYPE");
        return;
    }
    auto* director = EntityHelper::get_singleton_cmp<EventDirectorState>();
    EventType expected = parse_event_type(cmd.arg(0));
    if (director &&
        std::find(director->fired_today.begin(), director->fired_today.end(),
                  static_cast<int>(expected)) != director->fired_today.end()) {
        cmd.consume();
        return;
    }
    cmd.fail(fmt::format("assert_event_fired_today FAILED: {} not fired today",
                         event_type_name(expected)));
}

// assert_director_fired_today_count OP VALUE
static void cmd_assert_director_fired_count(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_director_fired_today_count requires OP VALUE");
        return;
    }
    auto* director = EntityHelper::get_singleton_cmp<EventDirectorState>();
    int actual = director ? (int) director->fired_today.size() : 0;
    if (!compare_op(actual, cmd.arg(0), cmd.arg_as<int>(1)))
        cmd.fail(fmt::format(
            "assert_director_fired_today_count failed: {} {} {} (actual: {})",
            actual, cmd.arg(0), cmd.arg_as<int>(1), actual));
    else
        cmd.consume();
}

// assert_active_event_count OP VALUE — live ActiveEvent entities.
static void cmd_assert_active_event_count(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_active_event_count requires OP VALUE");
        return;
    }
    int actual =
        (int) EntityQuery().whereHasComponent<ActiveEvent>().gen_count();
    if (!compare_op(actual, cmd.arg(0), cmd.arg_as<int>(1)))
        cmd.fail(fmt::format(
            "assert_active_event_count failed: {} {} {} (actual: {})", actual,
            cmd.arg(0), cmd.arg_as<int>(1), actual));
    else
        cmd.consume();
}

// assert_facility_offline TYPE / assert_facility_online TYPE
static void cmd_assert_facility_offline(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("assert_facility_offline requires TYPE");
        return;
    }
    FacilityType type = parse_facility_type(cmd.arg(0));
    if (facility_offline(type))
        cmd.consume();
    else
        cmd.fail(fmt::format("assert_facility_offline FAILED: {} is online",
                             cmd.arg(0)));
}

static void cmd_assert_facility_online(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("assert_facility_online requires TYPE");
        return;
    }
    FacilityType type = parse_facility_type(cmd.arg(0));
    if (!facility_offline(type))
        cmd.consume();
    else
        cmd.fail(fmt::format("assert_facility_online FAILED: {} is offline",
                             cmd.arg(0)));
}

// ── Heat / illness need probes ────────────────────────────────────────

// infect_agent X Z — give the agents on tile (X, Z) the illness
// (needs_heat). Used to seed one patient zero for viral spread.
static void cmd_infect_agent(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("infect_agent requires X Z");
        return;
    }
    int tx = cmd.arg_as<int>(0), tz = cmd.arg_as<int>(1);
    auto* grid = EntityHelper::get_singleton_cmp<Grid>();
    if (!grid) {
        cmd.fail("infect_agent: no grid");
        return;
    }
    int infected = 0;
    auto agents =
        EntityQuery().whereHasComponent<Agent, AgentNeeds, Transform>().gen();
    for (Entity& a : agents) {
        auto& tf = a.get<Transform>();
        auto [gx, gz] = grid->world_to_grid(tf.position.x, tf.position.y);
        if (gx == tx && gz == tz) {
            a.get<AgentNeeds>().needs_heat = true;
            infected++;
        }
    }
    if (infected == 0) {
        cmd.fail(fmt::format("infect_agent: no agents at ({},{})", tx, tz));
        return;
    }
    log_info("[E2E] infect_agent: {} agent(s) at ({},{})", infected, tx, tz);
    cmd.consume();
}

// assert_needs_heat_count OP VALUE — agents with the heat/illness need.
static void cmd_assert_needs_heat_count(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_needs_heat_count requires OP VALUE");
        return;
    }
    int count = 0;
    auto agents = EntityQuery().whereHasComponent<Agent, AgentNeeds>().gen();
    for (Entity& a : agents)
        if (a.get<AgentNeeds>().needs_heat) count++;
    if (!compare_op(count, cmd.arg(0), cmd.arg_as<int>(1)))
        cmd.fail(
            fmt::format("assert_needs_heat_count failed: {} {} {} (actual: {})",
                        count, cmd.arg(0), cmd.arg_as<int>(1), count));
    else
        cmd.consume();
}

// set_heat_timer VALUE / set_heat_threshold VALUE — all agents.
static void cmd_set_heat_timer(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("set_heat_timer requires VALUE");
        return;
    }
    float v = cmd.arg_as<float>(0);
    auto agents = EntityQuery().whereHasComponent<Agent, AgentNeeds>().gen();
    for (Entity& a : agents) a.get<AgentNeeds>().heat_timer = v;
    cmd.consume();
}

static void cmd_set_heat_threshold(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("set_heat_threshold requires VALUE");
        return;
    }
    float v = cmd.arg_as<float>(0);
    auto agents = EntityQuery().whereHasComponent<Agent, AgentNeeds>().gen();
    for (Entity& a : agents) {
        a.get<AgentNeeds>().heat_threshold = v;
        a.get<AgentNeeds>().needs_heat = false;
    }
    cmd.consume();
}

// assert_heat_timer OP VALUE — max heat_timer across agents (the test
// uses a single agent, so max == that agent's timer).
static void cmd_assert_heat_timer(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_heat_timer requires OP VALUE");
        return;
    }
    float actual = -1.f;
    auto agents = EntityQuery().whereHasComponent<Agent, AgentNeeds>().gen();
    for (Entity& a : agents)
        actual = std::max(actual, a.get<AgentNeeds>().heat_timer);
    if (actual < 0.f) {
        cmd.fail("assert_heat_timer: no agents with needs");
        return;
    }
    if (!compare_op_f(actual, cmd.arg(0), cmd.arg_as<float>(1)))
        cmd.fail(fmt::format(
            "assert_heat_timer failed: {:.2f} {} {:.2f} (actual: {:.2f})",
            actual, cmd.arg(0), cmd.arg_as<float>(1), actual));
    else
        cmd.consume();
}

// assert_min_agent_hp OP VALUE — min HP across agents with health.
static void cmd_assert_min_agent_hp(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_min_agent_hp requires OP VALUE");
        return;
    }
    float actual = 1e9f;
    auto agents = EntityQuery().whereHasComponent<Agent, AgentHealth>().gen();
    for (Entity& a : agents) actual = std::min(actual, a.get<AgentHealth>().hp);
    if (actual > 1e8f) {
        cmd.fail("assert_min_agent_hp: no agents with health");
        return;
    }
    if (!compare_op_f(actual, cmd.arg(0), cmd.arg_as<float>(1)))
        cmd.fail(fmt::format(
            "assert_min_agent_hp failed: {:.2f} {} {:.2f} (actual: {:.2f})",
            actual, cmd.arg(0), cmd.arg_as<float>(1), actual));
    else
        cmd.consume();
}

// assert_inspections_done OP VALUE
static void cmd_assert_inspections_done(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_inspections_done requires OP VALUE");
        return;
    }
    auto* insp = EntityHelper::get_singleton_cmp<InspectionState>();
    int actual = insp ? insp->inspections_done : 0;
    if (!compare_op(actual, cmd.arg(0), cmd.arg_as<int>(1)))
        cmd.fail(
            fmt::format("assert_inspections_done failed: {} {} {} (actual: {})",
                        actual, cmd.arg(0), cmd.arg_as<int>(1), actual));
    else
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

// ── Tycoon growth: price, injuries, inspections, milestones ─────────────

static void cmd_set_ticket_price(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("set_ticket_price requires VALUE");
        return;
    }
    auto* econ = EntityHelper::get_singleton_cmp<Economy>();
    if (!econ) {
        cmd.fail("set_ticket_price: no Economy");
        return;
    }
    econ->ticket_price =
        std::clamp(cmd.arg_as<int>(0), Economy::MIN_TICKET_PRICE,
                   Economy::MAX_TICKET_PRICE);
    cmd.consume();
}

static void cmd_ticket_price_up(testing::PendingE2ECommand& cmd) {
    auto* econ = EntityHelper::get_singleton_cmp<Economy>();
    if (econ) econ->adjust_price(Economy::TICKET_PRICE_STEP);
    cmd.consume();
}

static void cmd_ticket_price_down(testing::PendingE2ECommand& cmd) {
    auto* econ = EntityHelper::get_singleton_cmp<Economy>();
    if (econ) econ->adjust_price(-Economy::TICKET_PRICE_STEP);
    cmd.consume();
}

static void cmd_assert_ticket_price(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_ticket_price requires OP VALUE");
        return;
    }
    auto* econ = EntityHelper::get_singleton_cmp<Economy>();
    int actual = econ ? econ->ticket_price : 0;
    if (!compare_op(actual, cmd.arg(0), cmd.arg_as<int>(1)))
        cmd.fail(
            fmt::format("assert_ticket_price failed: {} {} {} (actual: {})",
                        actual, cmd.arg(0), cmd.arg_as<int>(1), actual));
    else
        cmd.consume();
}

static void cmd_assert_spawn_interval(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_spawn_interval requires OP VALUE");
        return;
    }
    auto* ss = EntityHelper::get_singleton_cmp<SpawnState>();
    float actual = ss ? ss->interval : 0.f;
    if (!compare_op_f(actual, cmd.arg(0), cmd.arg_as<float>(1)))
        cmd.fail(fmt::format(
            "assert_spawn_interval failed: {:.3f} {} {:.3f} (actual: {:.3f})",
            actual, cmd.arg(0), cmd.arg_as<float>(1), actual));
    else
        cmd.consume();
}

static void cmd_assert_injury_count(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_injury_count requires OP VALUE");
        return;
    }
    auto* gs = EntityHelper::get_singleton_cmp<GameState>();
    int actual = gs ? gs->injury_count : 0;
    if (!compare_op(actual, cmd.arg(0), cmd.arg_as<int>(1)))
        cmd.fail(
            fmt::format("assert_injury_count failed: {} {} {} (actual: {})",
                        actual, cmd.arg(0), cmd.arg_as<int>(1), actual));
    else
        cmd.consume();
}

static void cmd_set_injury_count(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("set_injury_count requires VALUE");
        return;
    }
    auto* gs = EntityHelper::get_singleton_cmp<GameState>();
    if (gs) gs->injury_count = cmd.arg_as<int>(0);
    cmd.consume();
}

static void cmd_add_injury_count(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("add_injury_count requires VALUE");
        return;
    }
    auto* gs = EntityHelper::get_singleton_cmp<GameState>();
    if (gs) gs->injury_count += cmd.arg_as<int>(0);
    cmd.consume();
}

static void cmd_add_death_count(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("add_death_count requires VALUE");
        return;
    }
    auto* gs = EntityHelper::get_singleton_cmp<GameState>();
    if (gs) gs->death_count += cmd.arg_as<int>(0);
    cmd.consume();
}

static void cmd_run_inspection(testing::PendingE2ECommand& cmd) {
    auto* insp = EntityHelper::get_singleton_cmp<InspectionState>();
    if (!insp) {
        cmd.fail("run_inspection: no InspectionState");
        return;
    }
    insp->force_inspection = true;
    cmd.consume();
}

static void cmd_assert_inspection_grade(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("assert_inspection_grade requires GRADE");
        return;
    }
    auto* insp = EntityHelper::get_singleton_cmp<InspectionState>();
    char actual = insp ? insp->last_grade : '-';
    if (cmd.arg(0).empty() || cmd.arg(0)[0] != actual)
        cmd.fail(
            fmt::format("assert_inspection_grade failed: expected {}, got {}",
                        cmd.arg(0), std::string(1, actual)));
    else
        cmd.consume();
}

static void cmd_assert_inspection_score(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_inspection_score requires OP VALUE");
        return;
    }
    auto* insp = EntityHelper::get_singleton_cmp<InspectionState>();
    int actual = insp ? insp->last_score : -1;
    if (!compare_op(actual, cmd.arg(0), cmd.arg_as<int>(1)))
        cmd.fail(
            fmt::format("assert_inspection_score failed: {} {} {} (actual: {})",
                        actual, cmd.arg(0), cmd.arg_as<int>(1), actual));
    else
        cmd.consume();
}

static void cmd_add_arrivals(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("add_arrivals requires VALUE");
        return;
    }
    auto* gs = EntityHelper::get_singleton_cmp<GameState>();
    if (gs) gs->total_arrivals += cmd.arg_as<int>(0);
    cmd.consume();
}

static void cmd_assert_total_arrivals(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_total_arrivals requires OP VALUE");
        return;
    }
    auto* gs = EntityHelper::get_singleton_cmp<GameState>();
    int actual = gs ? gs->total_arrivals : 0;
    if (!compare_op(actual, cmd.arg(0), cmd.arg_as<int>(1)))
        cmd.fail(
            fmt::format("assert_total_arrivals failed: {} {} {} (actual: {})",
                        actual, cmd.arg(0), cmd.arg_as<int>(1), actual));
    else
        cmd.consume();
}

// ── Staff ─────────────────────────────────────────────────────────────────

inline bool parse_staff_role(const std::string& s, StaffRole& out) {
    std::string lower = s;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower == "guard" || lower == "security") {
        out = StaffRole::Guard;
        return true;
    }
    if (lower == "medic" || lower == "med") {
        out = StaffRole::Medic;
        return true;
    }
    if (lower == "janitor" || lower == "cleaner") {
        out = StaffRole::Janitor;
        return true;
    }
    return false;
}

// hire_staff ROLE [X Z] — signing bonus deducted; X/Z default to the gate.
static void cmd_hire_staff(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("hire_staff requires ROLE [X Z]");
        return;
    }
    StaffRole role;
    if (!parse_staff_role(cmd.arg(0), role)) {
        cmd.fail(fmt::format("hire_staff: unknown role '{}'", cmd.arg(0)));
        return;
    }
    int x = cmd.has_args(3) ? cmd.arg_as<int>(1) : SPAWN_X;
    int z = cmd.has_args(3) ? cmd.arg_as<int>(2) : SPAWN_Z;
    hire_staff(role, x, z);
    cmd.consume();
}

// fire_staff ROLE — refunds nothing.
static void cmd_fire_staff(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("fire_staff requires ROLE");
        return;
    }
    StaffRole role;
    if (!parse_staff_role(cmd.arg(0), role)) {
        cmd.fail(fmt::format("fire_staff: unknown role '{}'", cmd.arg(0)));
        return;
    }
    fire_staff(role);
    cmd.consume();
}

static void cmd_assert_staff_count(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(3)) {
        cmd.fail("assert_staff_count requires ROLE OP VALUE");
        return;
    }
    StaffRole role;
    if (!parse_staff_role(cmd.arg(0), role)) {
        cmd.fail(
            fmt::format("assert_staff_count: unknown role '{}'", cmd.arg(0)));
        return;
    }
    int actual = staff_count(role);
    if (!compare_op(actual, cmd.arg(1), cmd.arg_as<int>(2)))
        cmd.fail(fmt::format(
            "assert_staff_count {} failed: {} {} {} (actual: {})", cmd.arg(0),
            actual, cmd.arg(1), cmd.arg_as<int>(2), actual));
    else
        cmd.consume();
}

// assert_staff_near ROLE X Z RADIUS — a staffer of the role within RADIUS
// tiles of (X, Z), e.g. a medic who walked to a collapsed agent.
static void cmd_assert_staff_near(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(4)) {
        cmd.fail("assert_staff_near requires ROLE X Z RADIUS");
        return;
    }
    StaffRole role;
    if (!parse_staff_role(cmd.arg(0), role)) {
        cmd.fail(
            fmt::format("assert_staff_near: unknown role '{}'", cmd.arg(0)));
        return;
    }
    int gx = cmd.arg_as<int>(1), gz = cmd.arg_as<int>(2);
    float radius = cmd.arg_as<float>(3);
    auto* grid = EntityHelper::get_singleton_cmp<Grid>();
    if (!grid) {
        cmd.fail("assert_staff_near: no grid");
        return;
    }
    ::vec2 tw = grid->grid_to_world(gx, gz);
    float rw = radius * TILESIZE;
    auto staff =
        EntityQuery().whereHasComponent<StaffMember, Transform>().gen();
    for (Entity& e : staff) {
        if (e.get<StaffMember>().role != role) continue;
        auto& tf = e.get<Transform>();
        float dx = tf.position.x - tw.x, dz = tf.position.y - tw.y;
        if (std::sqrt(dx * dx + dz * dz) <= rw) {
            cmd.consume();
            return;
        }
    }
    cmd.fail(fmt::format("assert_staff_near {} ({},{}) r={}: none nearby",
                         cmd.arg(0), gx, gz, radius));
}

static void cmd_assert_collapsed_count(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_collapsed_count requires OP VALUE");
        return;
    }
    int actual = (int) EntityQuery().whereHasComponent<Collapsed>().gen_count();
    if (!compare_op(actual, cmd.arg(0), cmd.arg_as<int>(1)))
        cmd.fail(
            fmt::format("assert_collapsed_count failed: {} {} {} (actual: {})",
                        actual, cmd.arg(0), cmd.arg_as<int>(1), actual));
    else
        cmd.consume();
}

// ── Lineup booking + festival tiers ──────────────────────────────────────

// book_act ID — same path as the sidebar panel button. Consumed even when
// the booking is refused (locked / duplicate / broke), so the test can
// assert the refusal toast and unchanged cash. Unknown IDs fail.
static void cmd_book_act(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("book_act requires ACT_ID");
        return;
    }
    if (find_bookable_act_index(cmd.arg(0)) < 0) {
        cmd.fail(fmt::format("book_act: unknown act '{}'", cmd.arg(0)));
        return;
    }
    book_act_by_id(cmd.arg(0));
    cmd.consume();
}

static void cmd_assert_booked_count(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_booked_count requires OP VALUE");
        return;
    }
    auto* lineup = EntityHelper::get_singleton_cmp<LineupState>();
    int actual = lineup ? (int) lineup->booked.size() : 0;
    if (!compare_op(actual, cmd.arg(0), cmd.arg_as<int>(1)))
        cmd.fail(
            fmt::format("assert_booked_count failed: {} {} {} (actual: {})",
                        actual, cmd.arg(0), cmd.arg_as<int>(1), actual));
    else
        cmd.consume();
}

static void cmd_assert_active_count(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_active_count requires OP VALUE");
        return;
    }
    auto* lineup = EntityHelper::get_singleton_cmp<LineupState>();
    int actual = lineup ? (int) lineup->active.size() : 0;
    if (!compare_op(actual, cmd.arg(0), cmd.arg_as<int>(1)))
        cmd.fail(
            fmt::format("assert_active_count failed: {} {} {} (actual: {})",
                        actual, cmd.arg(0), cmd.arg_as<int>(1), actual));
    else
        cmd.consume();
}

static void cmd_assert_booked_act(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("assert_booked_act requires ACT_ID");
        return;
    }
    int idx = find_bookable_act_index(cmd.arg(0));
    if (idx < 0) {
        cmd.fail(
            fmt::format("assert_booked_act: unknown act '{}'", cmd.arg(0)));
        return;
    }
    auto* lineup = EntityHelper::get_singleton_cmp<LineupState>();
    if (lineup && lineup->is_booked(idx))
        cmd.consume();
    else
        cmd.fail(fmt::format("assert_booked_act FAILED: '{}' not booked",
                             cmd.arg(0)));
}

static void cmd_assert_active_act(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("assert_active_act requires ACT_ID");
        return;
    }
    int idx = find_bookable_act_index(cmd.arg(0));
    if (idx < 0) {
        cmd.fail(
            fmt::format("assert_active_act: unknown act '{}'", cmd.arg(0)));
        return;
    }
    auto* lineup = EntityHelper::get_singleton_cmp<LineupState>();
    if (lineup && lineup->is_active(idx))
        cmd.consume();
    else
        cmd.fail(fmt::format("assert_active_act FAILED: '{}' not active",
                             cmd.arg(0)));
}

static void cmd_assert_not_booked_act(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("assert_not_booked_act requires ACT_ID");
        return;
    }
    int idx = find_bookable_act_index(cmd.arg(0));
    if (idx < 0) {
        cmd.fail(
            fmt::format("assert_not_booked_act: unknown act '{}'", cmd.arg(0)));
        return;
    }
    auto* lineup = EntityHelper::get_singleton_cmp<LineupState>();
    if (!lineup || !lineup->is_booked(idx))
        cmd.consume();
    else
        cmd.fail(fmt::format("assert_not_booked_act FAILED: '{}' is booked",
                             cmd.arg(0)));
}

static void cmd_assert_festival_tier(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_festival_tier requires OP VALUE");
        return;
    }
    auto* lineup = EntityHelper::get_singleton_cmp<LineupState>();
    int actual = lineup ? lineup->tier : 0;
    if (!compare_op(actual, cmd.arg(0), cmd.arg_as<int>(1)))
        cmd.fail(
            fmt::format("assert_festival_tier failed: {} {} {} (actual: {})",
                        actual, cmd.arg(0), cmd.arg_as<int>(1), actual));
    else
        cmd.consume();
}

static void cmd_assert_lineup_draw(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_lineup_draw requires OP VALUE");
        return;
    }
    auto* lineup = EntityHelper::get_singleton_cmp<LineupState>();
    float actual = lineup ? lineup->draw_mult() : 1.0f;
    if (!compare_op_f(actual, cmd.arg(0), cmd.arg_as<float>(1)))
        cmd.fail(fmt::format(
            "assert_lineup_draw failed: {:.3f} {} {:.3f} (actual: {:.3f})",
            actual, cmd.arg(0), cmd.arg_as<float>(1), actual));
    else
        cmd.consume();
}

static void cmd_assert_lineup_fun(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_lineup_fun requires OP VALUE");
        return;
    }
    auto* lineup = EntityHelper::get_singleton_cmp<LineupState>();
    float actual = lineup ? lineup->fun_mult() : 1.0f;
    if (!compare_op_f(actual, cmd.arg(0), cmd.arg_as<float>(1)))
        cmd.fail(fmt::format(
            "assert_lineup_fun failed: {:.3f} {} {:.3f} (actual: {:.3f})",
            actual, cmd.arg(0), cmd.arg_as<float>(1), actual));
    else
        cmd.consume();
}

static void cmd_assert_spawn_cap(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_spawn_cap requires OP VALUE");
        return;
    }
    auto* lineup = EntityHelper::get_singleton_cmp<LineupState>();
    int actual = max_spawn_cap_for_tier(lineup ? lineup->tier : 0);
    if (!compare_op(actual, cmd.arg(0), cmd.arg_as<int>(1)))
        cmd.fail(fmt::format("assert_spawn_cap failed: {} {} {} (actual: {})",
                             actual, cmd.arg(0), cmd.arg_as<int>(1), actual));
    else
        cmd.consume();
}

static void cmd_assert_act_unlocked(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("assert_act_unlocked requires ACT_ID");
        return;
    }
    int idx = find_bookable_act_index(cmd.arg(0));
    if (idx < 0) {
        cmd.fail(
            fmt::format("assert_act_unlocked: unknown act '{}'", cmd.arg(0)));
        return;
    }
    auto* lineup = EntityHelper::get_singleton_cmp<LineupState>();
    if (lineup && lineup->is_unlocked(idx))
        cmd.consume();
    else
        cmd.fail(fmt::format("assert_act_unlocked FAILED: '{}' is locked",
                             cmd.arg(0)));
}

// ── Park rating + land expansion ──────────────────────────────────────────

// set_park_rating VALUE — direct setter for deterministic tests. The
// rating then drifts toward its signal-derived target as usual.
static void cmd_set_park_rating(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("set_park_rating requires VALUE");
        return;
    }
    auto* rating = EntityHelper::get_singleton_cmp<ParkRatingState>();
    if (!rating) {
        cmd.fail("set_park_rating: no ParkRatingState");
        return;
    }
    rating->rating =
        std::clamp(cmd.arg_as<float>(0), static_cast<float>(PARK_RATING_MIN),
                   static_cast<float>(PARK_RATING_MAX));
    cmd.consume();
}

static void cmd_assert_park_rating(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_park_rating requires OP VALUE");
        return;
    }
    auto* rating = EntityHelper::get_singleton_cmp<ParkRatingState>();
    float actual = rating ? rating->rating : 0.f;
    if (!compare_op_f(actual, cmd.arg(0), cmd.arg_as<float>(1)))
        cmd.fail(fmt::format(
            "assert_park_rating failed: {:.2f} {} {:.2f} (actual: {:.2f})",
            actual, cmd.arg(0), cmd.arg_as<float>(1), actual));
    else
        cmd.consume();
}

static void cmd_assert_park_rating_trend(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("assert_park_rating_trend requires up|down|flat");
        return;
    }
    auto* rating = EntityHelper::get_singleton_cmp<ParkRatingState>();
    int actual = rating ? rating->trend() : 0;
    int want = cmd.arg(0) == "up" ? 1 : cmd.arg(0) == "down" ? -1 : 0;
    if (actual != want)
        cmd.fail(
            fmt::format("assert_park_rating_trend failed: expected {}, got {} "
                        "(rating {:.1f}, yesterday {:.1f})",
                        cmd.arg(0),
                        actual > 0   ? "up"
                        : actual < 0 ? "down"
                                     : "flat",
                        rating ? rating->rating : 0.f,
                        rating ? rating->yesterday_rating : 0.f));
    else
        cmd.consume();
}

static void cmd_assert_spawn_multiplier(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_spawn_multiplier requires OP VALUE");
        return;
    }
    auto* rating = EntityHelper::get_singleton_cmp<ParkRatingState>();
    float actual = rating ? rating->spawn_multiplier() : 1.0f;
    if (!compare_op_f(actual, cmd.arg(0), cmd.arg_as<float>(1)))
        cmd.fail(fmt::format(
            "assert_spawn_multiplier failed: {:.3f} {} {:.3f} (actual: {:.3f})",
            actual, cmd.arg(0), cmd.arg_as<float>(1), actual));
    else
        cmd.consume();
}

// buy_land east|south — same path as the land panel button. Consumed
// even when refused (cap / broke) so the test can assert the toast.
static void cmd_buy_land(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("buy_land requires east|south");
        return;
    }
    std::string dir = cmd.arg(0);
    std::transform(dir.begin(), dir.end(), dir.begin(), ::tolower);
    if (dir == "east")
        buy_land_expansion(0);
    else if (dir == "south")
        buy_land_expansion(1);
    else {
        cmd.fail(fmt::format("buy_land: unknown direction '{}'", cmd.arg(0)));
        return;
    }
    cmd.consume();
}

static void cmd_assert_expansions(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_expansions requires OP VALUE");
        return;
    }
    auto* grid = EntityHelper::get_singleton_cmp<Grid>();
    int actual = grid ? grid->total_expansions() : 0;
    if (!compare_op(actual, cmd.arg(0), cmd.arg_as<int>(1)))
        cmd.fail(fmt::format("assert_expansions failed: {} {} {} (actual: {})",
                             actual, cmd.arg(0), cmd.arg_as<int>(1), actual));
    else
        cmd.consume();
}

static void cmd_assert_expansion_cost(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_expansion_cost requires OP VALUE");
        return;
    }
    auto* grid = EntityHelper::get_singleton_cmp<Grid>();
    int actual = grid ? grid->next_expansion_cost() : 0;
    if (!compare_op(actual, cmd.arg(0), cmd.arg_as<int>(1)))
        cmd.fail(
            fmt::format("assert_expansion_cost failed: {} {} {} (actual: {})",
                        actual, cmd.arg(0), cmd.arg_as<int>(1), actual));
    else
        cmd.consume();
}

// set_camera_target X Z — the CameraInputSystem clamps the target to
// the bought park's fence rectangle on the next frame.
static void cmd_set_camera_target(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("set_camera_target requires X Z");
        return;
    }
    auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
    if (!cam) {
        cmd.fail("set_camera_target: no camera");
        return;
    }
    cam->cam.target.x = cmd.arg_as<float>(0);
    cam->cam.target.z = cmd.arg_as<float>(1);
    cmd.consume();
}

static void cmd_assert_camera_target(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_camera_target requires X Z");
        return;
    }
    auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
    if (!cam) {
        cmd.fail("assert_camera_target: no camera");
        return;
    }
    float want_x = cmd.arg_as<float>(0), want_z = cmd.arg_as<float>(1);
    if (!compare_op_f(cam->cam.target.x, "eq", want_x) ||
        !compare_op_f(cam->cam.target.z, "eq", want_z))
        cmd.fail(fmt::format(
            "assert_camera_target failed: expected ({:.1f}, {:.1f}), got "
            "({:.1f}, {:.1f})",
            want_x, want_z, cam->cam.target.x, cam->cam.target.z));
    else
        cmd.consume();
}

// set_view iso|top — switch the view mode directly (same state as the
// V key / HUD ISO-TOP button). assert_view checks it.
static void cmd_set_view(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("set_view requires iso|top");
        return;
    }
    auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
    if (!cam) {
        cmd.fail("set_view: no camera");
        return;
    }
    std::string mode = cmd.arg(0);
    std::transform(mode.begin(), mode.end(), mode.begin(), ::tolower);
    if (mode == "iso")
        cam->cam.iso_mode = true;
    else if (mode == "top")
        cam->cam.iso_mode = false;
    else {
        cmd.fail(fmt::format("set_view: unknown mode '{}'", cmd.arg(0)));
        return;
    }
    cmd.consume();
}

static void cmd_assert_view(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("assert_view requires iso|top");
        return;
    }
    auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
    if (!cam) {
        cmd.fail("assert_view: no camera");
        return;
    }
    std::string mode = cmd.arg(0);
    std::transform(mode.begin(), mode.end(), mode.begin(), ::tolower);
    bool want_iso = (mode == "iso");
    if (mode != "iso" && mode != "top") {
        cmd.fail(fmt::format("assert_view: unknown mode '{}'", cmd.arg(0)));
        return;
    }
    if (cam->cam.iso_mode != want_iso)
        cmd.fail(fmt::format("assert_view failed: expected {}, got {}", mode,
                             cam->cam.iso_mode ? "iso" : "top"));
    else
        cmd.consume();
}

// set_need_timer TYPE RATIO — set every agent's timer for one need to
// RATIO * threshold (e.g. bathroom 0.95 = urgent, thought-bubble range).
// Thresholds are per-agent randoms; a zero threshold is seeded to 100s
// so the ratio is still well-defined.
static void cmd_set_need_timer(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("set_need_timer requires TYPE RATIO");
        return;
    }
    std::string type = cmd.arg(0);
    std::transform(type.begin(), type.end(), type.begin(), ::tolower);
    float ratio = cmd.arg_as<float>(1);
    auto agents = EntityQuery().whereHasComponent<Agent, AgentNeeds>().gen();
    for (Entity& a : agents) {
        auto& needs = a.get<AgentNeeds>();
        if (type == "bathroom") {
            if (needs.bathroom_threshold <= 0.f)
                needs.bathroom_threshold = 100.f;
            needs.bathroom_timer = ratio * needs.bathroom_threshold;
        } else if (type == "food") {
            if (needs.food_threshold <= 0.f) needs.food_threshold = 100.f;
            needs.food_timer = ratio * needs.food_threshold;
        } else if (type == "heat") {
            if (needs.heat_threshold <= 0.f) needs.heat_threshold = 100.f;
            needs.heat_timer = ratio * needs.heat_threshold;
        } else if (type == "bar") {
            if (needs.bar_threshold <= 0.f) needs.bar_threshold = 100.f;
            needs.bar_timer = ratio * needs.bar_threshold;
        } else {
            cmd.fail(
                fmt::format("set_need_timer: unknown type '{}'", cmd.arg(0)));
            return;
        }
    }
    cmd.consume();
}

// ── Stage queue + bench rest + guest thoughts ────────────────────────────

// assert_queue_length OP N — agents currently waiting in the stage queue.
static void cmd_assert_queue_length(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_queue_length requires OP VALUE");
        return;
    }
    auto* qs = EntityHelper::get_singleton_cmp<StageQueueState>();
    int actual = qs ? qs->queue_length_cached : 0;
    if (!compare_op(actual, cmd.arg(0), cmd.arg_as<int>(1)))
        cmd.fail(
            fmt::format("assert_queue_length failed: {} {} {} (actual: {})",
                        actual, cmd.arg(0), cmd.arg_as<int>(1), actual));
    else
        cmd.consume();
}

// assert_queue_time OP SECONDS — average time the agents currently in
// line have waited (the queue-time stat shown on the Guests tab).
static void cmd_assert_queue_time(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_queue_time requires OP VALUE");
        return;
    }
    auto* qs = EntityHelper::get_singleton_cmp<StageQueueState>();
    float actual = qs ? qs->avg_queue_time() : 0.f;
    if (!compare_op_f(actual, cmd.arg(0), cmd.arg_as<float>(1)))
        cmd.fail(fmt::format(
            "assert_queue_time failed: {:.2f} {} {:.2f} (actual: {:.2f})",
            actual, cmd.arg(0), cmd.arg_as<float>(1), actual));
    else
        cmd.consume();
}

// assert_resting_count OP N — agents currently seated on a Bench.
static void cmd_assert_resting_count(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_resting_count requires OP VALUE");
        return;
    }
    int actual =
        (int) EntityQuery().whereHasComponent<RestingOnBench>().gen_count();
    if (!compare_op(actual, cmd.arg(0), cmd.arg_as<int>(1)))
        cmd.fail(
            fmt::format("assert_resting_count failed: {} {} {} (actual: {})",
                        actual, cmd.arg(0), cmd.arg_as<int>(1), actual));
    else
        cmd.consume();
}

// assert_top_thought KEY — the ranked #1 guest thought is KEY (toilet,
// value, crowded, sick, lineup), per compute_guest_thoughts().
static void cmd_assert_top_thought(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("assert_top_thought requires KEY");
        return;
    }
    const char* expected = guest_thought_text_for_key(cmd.arg(0));
    if (!expected) {
        cmd.fail(
            fmt::format("assert_top_thought: unknown key '{}'", cmd.arg(0)));
        return;
    }
    auto thoughts = compute_guest_thoughts();
    if (thoughts.empty() || std::strcmp(thoughts[0].text, expected) != 0)
        cmd.fail(
            fmt::format("assert_top_thought failed: expected '{}', got '{}'",
                        expected, thoughts.empty() ? "-" : thoughts[0].text));
    else
        cmd.consume();
}

// assert_thought_count KEY OP N — count for one canonical thought.
static void cmd_assert_thought_count(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(3)) {
        cmd.fail("assert_thought_count requires KEY OP VALUE");
        return;
    }
    const char* expected = guest_thought_text_for_key(cmd.arg(0));
    if (!expected) {
        cmd.fail(
            fmt::format("assert_thought_count: unknown key '{}'", cmd.arg(0)));
        return;
    }
    int actual = 0;
    for (const auto& t : compute_guest_thoughts())
        if (std::strcmp(t.text, expected) == 0) actual = t.count;
    if (!compare_op(actual, cmd.arg(1), cmd.arg_as<int>(2)))
        cmd.fail(fmt::format(
            "assert_thought_count {} failed: {} {} {} (actual: {})", cmd.arg(0),
            actual, cmd.arg(1), cmd.arg_as<int>(2), actual));
    else
        cmd.consume();
}

// set_watch_capacity N — how many agents may watch at once
// (default STAGE_WATCH_CAPACITY). Raising it promotes the queue;
// lowering it stops new watches until the count falls back under.
static void cmd_set_watch_capacity(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("set_watch_capacity requires VALUE");
        return;
    }
    auto* qs = EntityHelper::get_singleton_cmp<StageQueueState>();
    if (qs) qs->watch_capacity = std::max(0, cmd.arg_as<int>(0));
    cmd.consume();
}

// ── Park window tabs + money floaters ────────────────────────────────────

// set_window_tab NAME|INDEX — switch the park window's active tab
// (park 0, finances 1, guests 2, staff 3, lineup 4). Same effect as
// clicking the tab row; reset_game restores Park.
static int parse_window_tab(const std::string& arg, bool& ok) {
    std::string lower = arg;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    ok = true;
    if (lower == "park") return 0;
    if (lower == "finances" || lower == "finance") return 1;
    if (lower == "guests" || lower == "guest") return 2;
    if (lower == "staff") return 3;
    if (lower == "lineup" || lower == "line-up") return 4;
    try {
        int idx = std::stoi(lower);
        if (idx >= 0 && idx < 5) return idx;
    } catch (...) {
    }
    ok = false;
    return -1;
}

static void cmd_set_window_tab(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("set_window_tab requires NAME|INDEX");
        return;
    }
    bool ok = false;
    int tab = parse_window_tab(cmd.arg(0), ok);
    if (!ok) {
        cmd.fail(fmt::format("set_window_tab: unknown tab '{}'", cmd.arg(0)));
        return;
    }
    park_window_active_tab() = tab;
    cmd.consume();
}

static void cmd_assert_window_tab(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("assert_window_tab requires NAME|INDEX");
        return;
    }
    bool ok = false;
    int expected = parse_window_tab(cmd.arg(0), ok);
    if (!ok) {
        cmd.fail(
            fmt::format("assert_window_tab: unknown tab '{}'", cmd.arg(0)));
        return;
    }
    int actual = park_window_active_tab();
    if (actual != expected)
        cmd.fail(fmt::format("assert_window_tab failed: expected {}, got {}",
                             expected, actual));
    else
        cmd.consume();
}

// assert_floater_count OP VALUE — live money floaters (the
// visibility-independent floater assert; expect_text "+$N" is the
// visible-text one).
static void cmd_assert_floater_count(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_floater_count requires OP VALUE");
        return;
    }
    int actual = (int) money_floaters().items.size();
    if (!compare_op(actual, cmd.arg(0), cmd.arg_as<int>(1)))
        cmd.fail(
            fmt::format("assert_floater_count failed: {} {} {} "
                        "(actual: {})",
                        actual, cmd.arg(0), cmd.arg_as<int>(1), actual));
    else
        cmd.consume();
}

// ── Guest of Honor / VIP test hooks ───────────────────────────────────────

// Give one agent a VIP need (same semantics as force_need: the flag is
// set and the timer is at its threshold, so the goal system routes the
// agent to that facility at once). Heat uses threshold - 2s, as in
// force_need: ~2s from collapse if unserved.
static bool vip_set_need(Entity& e, const std::string& need) {
    if (e.is_missing<AgentNeeds>()) return false;
    auto& needs = e.get<AgentNeeds>();
    if (need == "bathroom") {
        needs.needs_bathroom = true;
        needs.bathroom_timer = needs.bathroom_threshold;
    } else if (need == "food") {
        needs.needs_food = true;
        needs.food_timer = needs.food_threshold;
    } else if (need == "bar") {
        needs.needs_bar = true;
        needs.bar_timer = needs.bar_threshold;
    } else if (need == "heat") {
        needs.needs_heat = true;
        needs.heat_timer = needs.heat_threshold - 2.f;
    } else {
        return false;
    }
    return true;
}

// spawn_vip X Z [NEED] — spawn one VIP guest at (X, Z) with NEED
// (food|bathroom|bar|heat, default food). Same ticket rule as
// spawn_agent: a VIP spawned on a Gate tile pays the ticket. Patience
// comes from EventDirectorState::vip_patience_seconds (see
// set_vip_patience). Use this instead of trigger_event when the test
// needs the VIP at a chosen spot (next to a facility, or far from
// one); the event path (VIPArrivalSystem) always spawns at the gate.
static void cmd_spawn_vip(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("spawn_vip requires X Z [NEED]");
        return;
    }
    std::string need = cmd.has_args(3) ? cmd.arg(2) : "food";
    std::transform(need.begin(), need.end(), need.begin(), ::tolower);
    if (need != "food" && need != "bathroom" && need != "bar" &&
        need != "heat") {
        cmd.fail(fmt::format("spawn_vip: unknown need '{}'", cmd.arg(2)));
        return;
    }
    int x = cmd.arg_as<int>(0), z = cmd.arg_as<int>(1);
    Entity& e = make_agent(x, z, FacilityType::Stage, STAGE_X + STAGE_SIZE / 2,
                           STAGE_Z + STAGE_SIZE / 2);
    e.addComponent<VIPGuest>();
    auto& vip = e.get<VIPGuest>();
    if (auto* director = EntityHelper::get_singleton_cmp<EventDirectorState>())
        vip.patience = director->vip_patience_seconds;
    vip_set_need(e, need);
    EntityHelper::merge_entity_arrays();
    cmd.consume();
}

// set_vip_patience SECONDS — patience for subsequently spawned VIPs
// (event arrivals and spawn_vip read it at spawn time). Lets a test
// exercise leaving-unhappy without waiting out the 60s game default.
static void cmd_set_vip_patience(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail("set_vip_patience requires SECONDS");
        return;
    }
    float seconds = cmd.arg_as<float>(0);
    if (seconds < 0.f) {
        cmd.fail("set_vip_patience: SECONDS must be >= 0");
        return;
    }
    auto* director = EntityHelper::get_singleton_cmp<EventDirectorState>();
    if (!director) {
        cmd.fail("set_vip_patience: no EventDirectorState");
        return;
    }
    director->vip_patience_seconds = seconds;
    cmd.consume();
}

static void cmd_assert_vip_count(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_vip_count requires OP VALUE");
        return;
    }
    int actual =
        (int) EntityQuery().whereHasComponent<Agent, VIPGuest>().gen_count();
    if (!compare_op(actual, cmd.arg(0), cmd.arg_as<int>(1)))
        cmd.fail(fmt::format("assert_vip_count failed: {} {} {} (actual: {})",
                             actual, cmd.arg(0), cmd.arg_as<int>(1), actual));
    else
        cmd.consume();
}

static void cmd_assert_vip_served_count(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_vip_served_count requires OP VALUE");
        return;
    }
    int actual = 0;
    auto agents = EntityQuery().whereHasComponent<Agent, VIPGuest>().gen();
    for (Entity& a : agents)
        if (a.get<VIPGuest>().served) actual++;
    if (!compare_op(actual, cmd.arg(0), cmd.arg_as<int>(1)))
        cmd.fail(
            fmt::format("assert_vip_served_count failed: {} {} {} (actual: {})",
                        actual, cmd.arg(0), cmd.arg_as<int>(1), actual));
    else
        cmd.consume();
}

static void cmd_assert_vip_penalized_count(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(2)) {
        cmd.fail("assert_vip_penalized_count requires OP VALUE");
        return;
    }
    int actual = 0;
    auto agents = EntityQuery().whereHasComponent<Agent, VIPGuest>().gen();
    for (Entity& a : agents)
        if (a.get<VIPGuest>().penalized) actual++;
    if (!compare_op(actual, cmd.arg(0), cmd.arg_as<int>(1)))
        cmd.fail(fmt::format(
            "assert_vip_penalized_count failed: {} {} {} (actual: {})", actual,
            cmd.arg(0), cmd.arg_as<int>(1), actual));
    else
        cmd.consume();
}

// ── Eventually asserts ───────────────────────────────────────────────────
//
// Opt-in eventual versions of the `assert_*` commands. Every plain
// `assert_*` handler calls `cmd.fail()` on its first failing evaluation,
// so a plain assert can never wait for agents to arrive, a facility to
// fill, or an event to fire — the script has to guess a `wait` long
// enough for the slowest machine instead. The `eventually_*` commands
// re-evaluate the same condition every frame until it holds or a
// caller-supplied timeout (seconds) expires.
//
// Mechanism: the handler probes the wrapped assert on a *fresh*
// PendingE2ECommand carrying only the inner args.
// `PendingE2ECommand::fail()` is sticky — it flips the command's
// private success flag to false with no way back — so probing the
// live command would poison it on the first failure even if the
// condition later held. The probe takes the failure; the live command
// only ever `consume()`s (probe passed) or `retry()`s (probe not yet
// passed), and only `fail()`s at the deadline or on a deterministic
// usage error. This is why the wrapper is restricted to `assert_*`
// handlers: they are read-only probes. Wrapping a side-effecting
// command (spawn, place, set_*) would execute its effect once per
// probe frame.
//
// Deadline: the vendor cleanup system fails any command still pending
// after `PendingE2ECommand::MAX_FRAMES` (30) frames, so a retrying
// command cannot outlive ~0.5s on `retry()` alone — `expect_text`
// retries the same way and is subject to the same cap. Each retry here
// therefore resets `cmd.frames_alive`, and the `eventually_*` handler
// owns the deadline itself, measured in wall-clock seconds from the
// first evaluation (stored in `cmd.seen_render`, the one per-command
// slot the vendor leaves untouched for non-`expect_text` commands:
// `expect_text` is the only family that reads it, and `eventually_*`
// never wraps that family). No static per-command state: the deadline
// lives on the command entity, so two `eventually_*` commands pending
// at once cannot clobber each other's timers the way a
// line-number-keyed static (the `drag_to` pattern) would.
//
// Forms:
//   eventually_assert <timeout_seconds> <assert_name> <assert_args...>
//   eventually_assert <assert_name> <assert_args...>   (default timeout)
// A timeout-last generic form is deliberately NOT supported: the
// generic wrapper does not know the inner assert's arity, so a trailing
// number is ambiguous with the inner assert's own trailing argument
// (most asserts end in a numeric VALUE). The concrete variants know
// their arity, so they accept the timeout first, last, or omitted:
//   eventually_assert_agent_count <op> <value> [timeout]
//   eventually_assert_agent_watching <op> <value> [timeout]
//   eventually_assert_agents_on_tiletype <type> <op> <count> [timeout]
//   eventually_assert_count is an alias for
//   eventually_assert_agent_count.

static constexpr float EVENTUALLY_DEFAULT_TIMEOUT_SECONDS = 5.0f;

// Strict float parse: the whole string must be one number (stoi/stof
// alone accept "5x" as 5, which would silently misread an op or a
// tiletype as a timeout).
inline bool eventually_parse_float_strict(const std::string& s, float& out) {
    if (s.empty()) return false;
    try {
        size_t used = 0;
        float v = std::stof(s, &used);
        if (used != s.size() || !std::isfinite(v)) return false;
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

// Shared eventual machinery: probe the inner assert, consume on pass,
// retry until the deadline, fail at the deadline (or immediately on a
// deterministic usage error, so a typo does not burn the whole
// timeout). The inner handler is looked up in the same registry the
// dispatch system uses, so `eventually_assert` works for any
// `assert_*` added later without touching this code.
static void eventually_assert_impl(testing::PendingE2ECommand& cmd,
                                   const std::string& inner_name,
                                   const std::vector<std::string>& inner_args,
                                   float timeout_seconds) {
    auto& reg = get_registry();
    auto it = reg.handlers.find(inner_name);
    if (it == reg.handlers.end()) {
        cmd.fail(
            fmt::format("eventually_assert: unknown assert '{}'", inner_name));
        return;
    }
    if (cmd.seen_render == -1) {
        cmd.seen_render = static_cast<long long>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
    }
    // Fresh probe, not a copy: PendingE2ECommand is move-only
    // (BaseComponent deletes the copy constructor), and a copy would
    // also carry the live command's seen_render / frames_alive. The
    // probe needs only the inner args; assert handlers read nothing
    // else off the command.
    testing::PendingE2ECommand probe;
    probe.name = inner_name;
    probe.args = inner_args;
    it->second(probe);
    if (probe.is_consumed() && probe.succeeded()) {
        log_info("[E2E] {} {} PASSED (eventual)", cmd.name, inner_name);
        cmd.consume();
        return;
    }
    if (probe.is_consumed() && !probe.succeeded()) {
        // Deterministic usage errors ("requires ...", "unknown ...")
        // will never become true by waiting: fail fast instead of
        // burning the timeout, and surface the inner message verbatim.
        if (probe.error_message.find("requires") != std::string::npos ||
            probe.error_message.find("unknown") != std::string::npos) {
            cmd.fail(probe.error_message);
            return;
        }
    }
    double elapsed_seconds =
        (static_cast<double>(
             std::chrono::duration_cast<std::chrono::microseconds>(
                 std::chrono::steady_clock::now().time_since_epoch())
                 .count()) -
         static_cast<double>(cmd.seen_render)) /
        1000000.0;
    if (elapsed_seconds >= timeout_seconds) {
        std::string last =
            probe.is_consumed() && !probe.succeeded() &&
                    !probe.error_message.empty()
                ? fmt::format(" last failure: {}", probe.error_message)
                : " condition never became true";
        cmd.fail(fmt::format(
            "{}: {} timed out after {:.1f}s (timeout {:.1f}s).{}", cmd.name,
            inner_name, elapsed_seconds, timeout_seconds, last));
        return;
    }
    // The vendor cleanup fails a command still pending after 30 frames;
    // this reset keeps a legitimately long wait alive until OUR
    // deadline above fires.
    cmd.frames_alive = 0;
    cmd.retry();
}

// eventually_assert [TIMEOUT_SECONDS] ASSERT_NAME [ARGS...]
static void cmd_eventually_assert(testing::PendingE2ECommand& cmd) {
    if (!cmd.has_args(1)) {
        cmd.fail(
            "eventually_assert requires [TIMEOUT_SECONDS] ASSERT_NAME "
            "[ARGS...]");
        return;
    }
    float timeout = EVENTUALLY_DEFAULT_TIMEOUT_SECONDS;
    std::string inner_name;
    std::vector<std::string> inner_args;
    float first_num = 0.f;
    if (cmd.has_args(2) &&
        eventually_parse_float_strict(cmd.arg(0), first_num)) {
        if (first_num <= 0.f) {
            cmd.fail("eventually_assert: TIMEOUT_SECONDS must be > 0");
            return;
        }
        timeout = first_num;
        inner_name = cmd.arg(1);
        inner_args.assign(cmd.args.begin() + 2, cmd.args.end());
    } else {
        inner_name = cmd.arg(0);
        inner_args.assign(cmd.args.begin() + 1, cmd.args.end());
    }
    if (inner_name.rfind("assert_", 0) != 0) {
        cmd.fail(fmt::format(
            "eventually_assert: can only wrap assert_* commands, got '{}'",
            inner_name));
        return;
    }
    if (inner_name.rfind("eventually_", 0) == 0) {
        cmd.fail(
            "eventually_assert: nesting eventually_ asserts is not "
            "supported");
        return;
    }
    if (get_registry().handlers.find(inner_name) ==
        get_registry().handlers.end()) {
        cmd.fail(
            fmt::format("eventually_assert: unknown assert '{}'", inner_name));
        return;
    }
    eventually_assert_impl(cmd, inner_name, inner_args, timeout);
}

// Parse a concrete eventual variant: the base assert's args, plus an
// optional timeout either first or last (both are unambiguous here
// because the base arity is known: a leading number can only be the
// timeout, since no base arg of these asserts is numeric-first).
// Returns false after failing `cmd` on a malformed invocation.
static bool eventually_concrete_args(testing::PendingE2ECommand& cmd,
                                     size_t base_arity,
                                     std::vector<std::string>& inner_args,
                                     float& timeout_seconds) {
    timeout_seconds = EVENTUALLY_DEFAULT_TIMEOUT_SECONDS;
    if (cmd.args.size() == base_arity) {
        inner_args = cmd.args;
        return true;
    }
    if (cmd.args.size() == base_arity + 1) {
        float first_num = 0.f;
        if (eventually_parse_float_strict(cmd.arg(0), first_num)) {
            if (first_num <= 0.f) {
                cmd.fail("eventually_assert: TIMEOUT_SECONDS must be > 0");
                return false;
            }
            timeout_seconds = first_num;
            inner_args.assign(cmd.args.begin() + 1, cmd.args.end());
            return true;
        }
        float last_num = 0.f;
        if (!eventually_parse_float_strict(cmd.arg(cmd.args.size() - 1),
                                           last_num) ||
            last_num <= 0.f) {
            cmd.fail(fmt::format(
                "eventually_assert: trailing timeout must be a positive "
                "number, got '{}'",
                cmd.arg(cmd.args.size() - 1)));
            return false;
        }
        timeout_seconds = last_num;
        inner_args.assign(cmd.args.begin(), cmd.args.end() - 1);
        return true;
    }
    cmd.fail(fmt::format(
        "eventually_assert: expected {} assert arg(s) plus an optional "
        "timeout, got {} arg(s)",
        base_arity, cmd.args.size()));
    return false;
}

static void cmd_eventually_assert_agent_count(testing::PendingE2ECommand& cmd) {
    std::vector<std::string> inner_args;
    float timeout = EVENTUALLY_DEFAULT_TIMEOUT_SECONDS;
    if (!eventually_concrete_args(cmd, 2, inner_args, timeout)) return;
    eventually_assert_impl(cmd, "assert_agent_count", inner_args, timeout);
}

static void cmd_eventually_assert_agent_watching(
    testing::PendingE2ECommand& cmd) {
    std::vector<std::string> inner_args;
    float timeout = EVENTUALLY_DEFAULT_TIMEOUT_SECONDS;
    if (!eventually_concrete_args(cmd, 2, inner_args, timeout)) return;
    eventually_assert_impl(cmd, "assert_agent_watching", inner_args, timeout);
}

static void cmd_eventually_assert_agents_on_tiletype(
    testing::PendingE2ECommand& cmd) {
    std::vector<std::string> inner_args;
    float timeout = EVENTUALLY_DEFAULT_TIMEOUT_SECONDS;
    if (!eventually_concrete_args(cmd, 3, inner_args, timeout)) return;
    eventually_assert_impl(cmd, "assert_agents_on_tiletype", inner_args,
                           timeout);
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
    r.add("place_path_rect", cmd_place_path_rect);
    r.add("assert_tile_height", cmd_assert_tile_height);
    r.add("assert_lamp_glow", cmd_assert_lamp_glow);
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
    r.add("get_brightness", cmd_get_brightness);
    r.add("assert_brightness", cmd_assert_brightness);
    r.add("assert_rain_active", cmd_assert_rain_active);
    r.add("assert_rain_inactive", cmd_assert_rain_inactive);
    r.add("get_rain_streak_count", cmd_get_rain_streak_count);
    r.add("assert_rain_streak_count", cmd_assert_rain_streak_count);
    r.add("assert_weather_tint", cmd_assert_weather_tint);
    r.add("assert_night_glow", cmd_assert_night_glow);
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
    r.add("set_cash", cmd_set_cash);
    r.add("assert_cash", cmd_assert_cash);
    r.add("assert_total_earned", cmd_assert_total_earned);
    r.add("assert_total_spent", cmd_assert_total_spent);
    r.add("borrow_loan", cmd_borrow_loan);
    r.add("repay_loan", cmd_repay_loan);
    r.add("assert_debt", cmd_assert_debt);
    r.add("assert_history_count", cmd_assert_history_count);
    r.add("assert_history_day", cmd_assert_history_day);
    r.add("charge_wages", cmd_charge_wages);
    r.add("demolish_at", cmd_demolish_at);
    r.add("set_all_agent_hp", cmd_set_all_agent_hp);
    r.add("perf_start", cmd_perf_start);
    r.add("perf_report", cmd_perf_report);
    r.add("assert_fps", cmd_assert_fps);
    r.add("assert_min_fps", cmd_assert_min_fps);
    r.add("assert_profile_sampled", cmd_assert_profile_sampled);
    r.add("assert_nux_active", cmd_assert_nux_active);
    r.add("assert_nux_inactive", cmd_assert_nux_inactive);
    r.add("assert_nux_count", cmd_assert_nux_count);
    r.add("dismiss_nux", cmd_dismiss_nux);
    r.add("assert_pixel", cmd_assert_pixel);
    r.add("assert_region_not_blank", cmd_assert_region_not_blank);
    r.add("set_zoom", cmd_set_zoom);
    r.add("assert_zoom", cmd_assert_zoom);
    r.add("trigger_event", cmd_trigger_event);
    r.add("assert_event_active", cmd_assert_event_active);
    r.add("assert_event_inactive", cmd_assert_event_inactive);
    r.add("set_event_seed", cmd_set_event_seed);
    r.add("roll_events", cmd_roll_events);
    r.add("assert_director_no_repeat", cmd_assert_director_no_repeat);
    r.add("assert_event_fired_today", cmd_assert_event_fired_today);
    r.add("assert_director_fired_today_count", cmd_assert_director_fired_count);
    r.add("assert_active_event_count", cmd_assert_active_event_count);
    r.add("assert_facility_offline", cmd_assert_facility_offline);
    r.add("assert_facility_online", cmd_assert_facility_online);
    r.add("infect_agent", cmd_infect_agent);
    r.add("assert_needs_heat_count", cmd_assert_needs_heat_count);
    r.add("set_heat_timer", cmd_set_heat_timer);
    r.add("set_heat_threshold", cmd_set_heat_threshold);
    r.add("assert_heat_timer", cmd_assert_heat_timer);
    r.add("assert_min_agent_hp", cmd_assert_min_agent_hp);
    r.add("assert_inspections_done", cmd_assert_inspections_done);
    r.add("assert_day_number", cmd_assert_day_number);
    r.add("set_day_number", cmd_set_day_number);
    r.add("assert_spawn_rate", cmd_assert_spawn_rate);
    r.add("assert_toast_contains", cmd_assert_toast_contains);
    r.add("assert_no_toast", cmd_assert_no_toast);
    r.add("save_game", cmd_save_game);
    r.add("load_game", cmd_load_game);
    r.add("assert_save_exists", cmd_assert_save_exists);
    r.add("delete_save", cmd_delete_save);
    r.add("set_ticket_price", cmd_set_ticket_price);
    r.add("ticket_price_up", cmd_ticket_price_up);
    r.add("ticket_price_down", cmd_ticket_price_down);
    r.add("assert_ticket_price", cmd_assert_ticket_price);
    r.add("assert_spawn_interval", cmd_assert_spawn_interval);
    r.add("assert_injury_count", cmd_assert_injury_count);
    r.add("set_injury_count", cmd_set_injury_count);
    r.add("add_injury_count", cmd_add_injury_count);
    r.add("add_death_count", cmd_add_death_count);
    r.add("run_inspection", cmd_run_inspection);
    r.add("assert_inspection_grade", cmd_assert_inspection_grade);
    r.add("assert_inspection_score", cmd_assert_inspection_score);
    r.add("add_arrivals", cmd_add_arrivals);
    r.add("assert_total_arrivals", cmd_assert_total_arrivals);
    r.add("assert_collapsed_count", cmd_assert_collapsed_count);
    r.add("hire_staff", cmd_hire_staff);
    r.add("fire_staff", cmd_fire_staff);
    r.add("assert_staff_count", cmd_assert_staff_count);
    r.add("assert_staff_near", cmd_assert_staff_near);
    r.add("book_act", cmd_book_act);
    r.add("assert_booked_count", cmd_assert_booked_count);
    r.add("assert_active_count", cmd_assert_active_count);
    r.add("assert_booked_act", cmd_assert_booked_act);
    r.add("assert_active_act", cmd_assert_active_act);
    r.add("assert_not_booked_act", cmd_assert_not_booked_act);
    r.add("assert_festival_tier", cmd_assert_festival_tier);
    r.add("assert_lineup_draw", cmd_assert_lineup_draw);
    r.add("assert_lineup_fun", cmd_assert_lineup_fun);
    r.add("assert_spawn_cap", cmd_assert_spawn_cap);
    r.add("assert_act_unlocked", cmd_assert_act_unlocked);
    r.add("set_park_rating", cmd_set_park_rating);
    r.add("assert_park_rating", cmd_assert_park_rating);
    r.add("assert_park_rating_trend", cmd_assert_park_rating_trend);
    r.add("assert_spawn_multiplier", cmd_assert_spawn_multiplier);
    r.add("buy_land", cmd_buy_land);
    r.add("assert_expansions", cmd_assert_expansions);
    r.add("assert_expansion_cost", cmd_assert_expansion_cost);
    r.add("set_camera_target", cmd_set_camera_target);
    r.add("assert_camera_target", cmd_assert_camera_target);
    r.add("set_view", cmd_set_view);
    r.add("assert_view", cmd_assert_view);
    r.add("set_need_timer", cmd_set_need_timer);
    r.add("set_window_tab", cmd_set_window_tab);
    r.add("assert_window_tab", cmd_assert_window_tab);
    r.add("assert_floater_count", cmd_assert_floater_count);
    r.add("assert_queue_length", cmd_assert_queue_length);
    r.add("assert_queue_time", cmd_assert_queue_time);
    r.add("assert_resting_count", cmd_assert_resting_count);
    r.add("assert_top_thought", cmd_assert_top_thought);
    r.add("assert_thought_count", cmd_assert_thought_count);
    r.add("set_watch_capacity", cmd_set_watch_capacity);
    r.add("eventually_assert", cmd_eventually_assert);
    r.add("eventually_assert_agent_count", cmd_eventually_assert_agent_count);
    r.add("eventually_assert_count", cmd_eventually_assert_agent_count);
    r.add("eventually_assert_agent_watching",
          cmd_eventually_assert_agent_watching);
    r.add("eventually_assert_agents_on_tiletype",
          cmd_eventually_assert_agents_on_tiletype);
    r.add("spawn_vip", cmd_spawn_vip);
    r.add("set_vip_patience", cmd_set_vip_patience);
    r.add("assert_vip_count", cmd_assert_vip_count);
    r.add("assert_vip_served_count", cmd_assert_vip_served_count);
    r.add("assert_vip_penalized_count", cmd_assert_vip_penalized_count);
}

void register_e2e_systems(SystemManager& sm) {
    testing::register_builtin_handlers(sm);
    // dump_profile / expect_fps_above / expect_p99_below. Opt-in in
    // afterhours, so it has to be registered explicitly.
    testing::perf_commands::register_perf_commands(sm);
    init_e2e_registry();
    sm.register_update_system(std::make_unique<E2EDispatchSystem>());
    testing::register_unknown_handler(sm);
    testing::register_cleanup(sm);
}
