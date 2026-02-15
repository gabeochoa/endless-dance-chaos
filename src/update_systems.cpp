// AFTER_HOURS_REPLACE_LOGGING and log.h must come first
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "afterhours/src/core/entity_query.h"
#include "components.h"
#include "engine/random_engine.h"
#include "entity_makers.h"
#include "systems.h"

struct CameraInputSystem : System<ProvidesCamera> {
    void for_each_with(Entity&, ProvidesCamera& cam, float dt) override {
        cam.cam.handle_input(dt);
    }
};

// Handle path drawing (rectangle drag) and demolish mode
struct PathBuildSystem : System<> {
    void once(float) override {
        auto* pds = EntityHelper::get_singleton_cmp<PathDrawState>();
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!pds || !grid) return;

        // Toggle demolish mode with X key
        if (action_pressed(InputAction::ToggleDemolish)) {
            pds->demolish_mode = !pds->demolish_mode;
            pds->is_drawing = false;  // Cancel any pending draw
            log_info("Demolish mode: {}", pds->demolish_mode ? "ON" : "OFF");
        }

        // Cancel with Escape or right-click
        if (action_pressed(InputAction::Cancel) ||
            input::is_mouse_button_pressed(raylib::MOUSE_BUTTON_RIGHT)) {
            if (pds->is_drawing) {
                pds->is_drawing = false;
            } else if (pds->demolish_mode) {
                pds->demolish_mode = false;
            }
            return;
        }

        if (!pds->hover_valid) return;

        // Left-click actions
        if (input::is_mouse_button_pressed(raylib::MOUSE_BUTTON_LEFT)) {
            if (pds->demolish_mode) {
                // Demolish: click on path -> revert to grass
                Tile& tile = grid->at(pds->hover_x, pds->hover_z);
                if (tile.type == TileType::Path) {
                    tile.type = TileType::Grass;
                }
            } else if (!pds->is_drawing) {
                // Start rectangle: set first corner
                pds->start_x = pds->hover_x;
                pds->start_z = pds->hover_z;
                pds->is_drawing = true;
            } else {
                // Confirm rectangle: fill all tiles with path
                int min_x, min_z, max_x, max_z;
                pds->get_rect(min_x, min_z, max_x, max_z);

                for (int z = min_z; z <= max_z; z++) {
                    for (int x = min_x; x <= max_x; x++) {
                        if (grid->in_bounds(x, z)) {
                            grid->at(x, z).type = TileType::Path;
                        }
                    }
                }
                pds->is_drawing = false;
            }
        }
    }
};

// Greedy neighbor pathfinding: pick the adjacent tile closest to goal,
// preferring Path tiles over Grass when distances are equal.
static std::pair<int, int> pick_next_tile(int cur_x, int cur_z, int goal_x,
                                          int goal_z, const Grid& grid) {
    static constexpr int dx[] = {1, -1, 0, 0};
    static constexpr int dz[] = {0, 0, 1, -1};

    int best_x = cur_x;
    int best_z = cur_z;
    int best_dist = std::abs(cur_x - goal_x) + std::abs(cur_z - goal_z);
    bool best_is_path = false;

    for (int i = 0; i < 4; i++) {
        int nx = cur_x + dx[i];
        int nz = cur_z + dz[i];
        if (!grid.in_bounds(nx, nz)) continue;

        TileType type = grid.at(nx, nz).type;
        // Can't walk through fences
        if (type == TileType::Fence) continue;

        int dist = std::abs(nx - goal_x) + std::abs(nz - goal_z);
        bool is_path = (type == TileType::Path || type == TileType::Gate);

        // Pick if closer, or same distance but on path and current best isn't
        if (dist < best_dist ||
            (dist == best_dist && is_path && !best_is_path)) {
            best_x = nx;
            best_z = nz;
            best_dist = dist;
            best_is_path = is_path;
        }
    }

    return {best_x, best_z};
}

// Density-based speed modifier: slows agents in dangerous zones (75-90%)
static float density_speed_modifier(float density_ratio) {
    if (density_ratio < DENSITY_DANGEROUS) return 1.0f;
    if (density_ratio >= DENSITY_CRITICAL) return 0.1f;  // near-stop
    // Linear interpolation between dangerous and critical
    float t = (density_ratio - DENSITY_DANGEROUS) /
              (DENSITY_CRITICAL - DENSITY_DANGEROUS);
    return 1.0f - (t * 0.9f);  // 1.0 -> 0.1
}

// Move agents toward their target using greedy pathfinding
struct AgentMovementSystem : System<Agent, Transform> {
    void for_each_with(Entity& e, Agent& agent, Transform& tf,
                       float dt) override {
        // Don't move if being serviced or watching stage
        if (!e.is_missing<BeingServiced>()) return;
        if (!e.is_missing<WatchingStage>()) return;
        if (agent.target_grid_x < 0 || agent.target_grid_z < 0) return;

        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;

        // Get current grid position
        auto [cur_gx, cur_gz] =
            grid->world_to_grid(tf.position.x, tf.position.y);

        // Already at target?
        if (cur_gx == agent.target_grid_x && cur_gz == agent.target_grid_z)
            return;

        // Adjust speed based on current terrain
        TileType cur_type = TileType::Grass;
        if (grid->in_bounds(cur_gx, cur_gz)) {
            cur_type = grid->at(cur_gx, cur_gz).type;
        }
        agent.speed = (cur_type == TileType::Path || cur_type == TileType::Gate)
                          ? SPEED_PATH
                          : SPEED_GRASS;

        // Apply density-based slowdown
        if (grid->in_bounds(cur_gx, cur_gz)) {
            float density = grid->at(cur_gx, cur_gz).agent_count /
                            static_cast<float>(MAX_AGENTS_PER_TILE);
            agent.speed *= density_speed_modifier(density);
        }

        // Apply global speed multiplier (for E2E testing)
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        if (gs) agent.speed *= gs->speed_multiplier;

        // Pick next tile via greedy neighbor
        auto [next_x, next_z] = pick_next_tile(
            cur_gx, cur_gz, agent.target_grid_x, agent.target_grid_z, *grid);

        // Move toward center of next tile
        ::vec2 target_world = grid->grid_to_world(next_x, next_z);
        float dx = target_world.x - tf.position.x;
        float dz = target_world.y - tf.position.y;  // vec2.y = world z
        float dist = std::sqrt(dx * dx + dz * dz);

        if (dist > 0.01f) {
            float step = agent.speed * TILESIZE * dt;
            if (step > dist) step = dist;
            tf.position.x += (dx / dist) * step;
            tf.position.y += (dz / dist) * step;
        }
    }
};

// Spawn agents at the spawn point on a timer
struct SpawnAgentSystem : System<> {
    void once(float dt) override {
        auto* ss = EntityHelper::get_singleton_cmp<SpawnState>();
        if (!ss || !ss->enabled) return;

        ss->timer += dt;
        if (ss->timer >= ss->interval) {
            ss->timer -= ss->interval;

            // Spawn agent heading toward the stage center
            int stage_center_x = STAGE_X + STAGE_SIZE / 2;
            int stage_center_z = STAGE_Z + STAGE_SIZE / 2;
            make_agent(SPAWN_X, SPAWN_Z, FacilityType::Stage, stage_center_x,
                       stage_center_z);
        }
    }
};

// Find the center of the nearest facility of a given type
static std::pair<int, int> find_nearest_facility(int from_x, int from_z,
                                                 TileType type,
                                                 const Grid& grid) {
    int best_x = -1, best_z = -1;
    int best_dist = std::numeric_limits<int>::max();

    for (int z = 0; z < MAP_SIZE; z++) {
        for (int x = 0; x < MAP_SIZE; x++) {
            if (grid.at(x, z).type != type) continue;
            int dist = std::abs(x - from_x) + std::abs(z - from_z);
            if (dist < best_dist) {
                best_dist = dist;
                best_x = x;
                best_z = z;
            }
        }
    }
    return {best_x, best_z};
}

// Check if a facility tile is "full" (too many agents)
static bool facility_is_full(int gx, int gz, const Grid& grid) {
    if (!grid.in_bounds(gx, gz)) return true;
    return grid.at(gx, gz).agent_count >= FACILITY_MAX_AGENTS;
}

// Tick need timers and set need flags
struct NeedTickSystem : System<Agent, AgentNeeds> {
    void for_each_with(Entity& e, Agent&, AgentNeeds& needs,
                       float dt) override {
        // Don't tick timers while being serviced or watching
        if (!e.is_missing<BeingServiced>()) return;
        if (!e.is_missing<WatchingStage>()) return;

        needs.bathroom_timer += dt;
        needs.food_timer += dt;

        if (!needs.needs_bathroom &&
            needs.bathroom_timer >= needs.bathroom_threshold) {
            needs.needs_bathroom = true;
        }
        if (!needs.needs_food && needs.food_timer >= needs.food_threshold) {
            needs.needs_food = true;
        }
    }
};

// Select agent's goal based on need priority: bathroom > food > stage
struct UpdateAgentGoalSystem : System<Agent, AgentNeeds, Transform> {
    void for_each_with(Entity& e, Agent& agent, AgentNeeds& needs,
                       Transform& tf, float) override {
        // Don't change goal while being serviced or watching
        if (!e.is_missing<BeingServiced>()) return;
        if (!e.is_missing<WatchingStage>()) return;

        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;

        auto [cur_gx, cur_gz] =
            grid->world_to_grid(tf.position.x, tf.position.y);

        if (needs.needs_bathroom) {
            if (agent.want != FacilityType::Bathroom) {
                auto [bx, bz] = find_nearest_facility(
                    cur_gx, cur_gz, TileType::Bathroom, *grid);
                if (bx >= 0) {
                    agent.want = FacilityType::Bathroom;
                    agent.target_grid_x = bx;
                    agent.target_grid_z = bz;
                }
            }
        } else if (needs.needs_food) {
            if (agent.want != FacilityType::Food) {
                auto [fx, fz] = find_nearest_facility(cur_gx, cur_gz,
                                                      TileType::Food, *grid);
                if (fx >= 0) {
                    agent.want = FacilityType::Food;
                    agent.target_grid_x = fx;
                    agent.target_grid_z = fz;
                }
            }
        } else {
            // Default: go to stage
            if (agent.want != FacilityType::Stage) {
                agent.want = FacilityType::Stage;
                agent.target_grid_x = STAGE_X + STAGE_SIZE / 2;
                agent.target_grid_z = STAGE_Z + STAGE_SIZE / 2;
            }
        }
    }
};

// When agent reaches the stage area, start watching
struct StageWatchingSystem : System<Agent, Transform> {
    void for_each_with(Entity& e, Agent& agent, Transform& tf,
                       float dt) override {
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;

        // Already watching? Tick timer.
        if (!e.is_missing<WatchingStage>()) {
            auto& ws = e.get<WatchingStage>();
            ws.watch_timer += dt;
            if (ws.watch_timer >= ws.watch_duration) {
                e.removeComponent<WatchingStage>();
                // Done watching, needs will drive next goal
            }
            return;
        }

        // Being serviced? Skip.
        if (!e.is_missing<BeingServiced>()) return;

        // Only start watching if heading to stage
        if (agent.want != FacilityType::Stage) return;

        // Check if within watch radius of stage center
        float stage_cx = (STAGE_X + STAGE_SIZE / 2.0f) * TILESIZE;
        float stage_cz = (STAGE_Z + STAGE_SIZE / 2.0f) * TILESIZE;
        float dx = tf.position.x - stage_cx;
        float dz = tf.position.y - stage_cz;
        float dist = std::sqrt(dx * dx + dz * dz);

        if (dist <= STAGE_WATCH_RADIUS * TILESIZE) {
            auto& rng = RandomEngine::get();
            e.addComponent<WatchingStage>();
            e.get<WatchingStage>().watch_duration = rng.get_float(30.f, 120.f);
        }
    }
};

// Handle agents arriving at facilities: absorb, service, release
struct FacilityServiceSystem : System<Agent, AgentNeeds, Transform> {
    void for_each_with(Entity& e, Agent& agent, AgentNeeds& needs,
                       Transform& tf, float dt) override {
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;

        // Already being serviced? Tick the timer.
        if (!e.is_missing<BeingServiced>()) {
            auto& bs = e.get<BeingServiced>();
            bs.time_remaining -= dt;
            if (bs.time_remaining <= 0.f) {
                // Service complete - clear need, reset timer
                auto& rng = RandomEngine::get();
                if (bs.facility_type == FacilityType::Bathroom) {
                    needs.needs_bathroom = false;
                    needs.bathroom_timer = 0.f;
                    needs.bathroom_threshold = rng.get_float(30.f, 90.f);
                } else if (bs.facility_type == FacilityType::Food) {
                    needs.needs_food = false;
                    needs.food_timer = 0.f;
                    needs.food_threshold = rng.get_float(45.f, 120.f);
                }

                // Reappear adjacent to facility
                // (just offset by 1 tile in -x direction)
                tf.position.x = bs.facility_grid_x * TILESIZE - TILESIZE;
                tf.position.y = bs.facility_grid_z * TILESIZE;

                e.removeComponent<BeingServiced>();

                // Reset goal to stage
                agent.want = FacilityType::Stage;
                agent.target_grid_x = STAGE_X + STAGE_SIZE / 2;
                agent.target_grid_z = STAGE_Z + STAGE_SIZE / 2;
            }
            return;
        }

        // Not serviced yet -- check if we're on a facility tile
        if (agent.want == FacilityType::Stage) return;  // not seeking facility

        auto [gx, gz] = grid->world_to_grid(tf.position.x, tf.position.y);
        if (!grid->in_bounds(gx, gz)) return;

        TileType cur_type = grid->at(gx, gz).type;

        // Check if we've arrived at the correct facility type
        bool at_target = false;
        if (agent.want == FacilityType::Bathroom &&
            cur_type == TileType::Bathroom) {
            at_target = true;
        } else if (agent.want == FacilityType::Food &&
                   cur_type == TileType::Food) {
            at_target = true;
        }

        if (at_target) {
            // Check capacity
            if (facility_is_full(gx, gz, *grid)) {
                // Facility full -- for now, just keep waiting
                return;
            }

            // Start service
            e.addComponent<BeingServiced>();
            auto& bs = e.get<BeingServiced>();
            bs.facility_grid_x = gx;
            bs.facility_grid_z = gz;
            bs.facility_type = agent.want;
            bs.time_remaining = SERVICE_TIME;
        }
    }
};

// Update per-tile agent density each frame
struct UpdateTileDensitySystem : System<> {
    void once(float) override {
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;

        // Clear all counts
        for (auto& tile : grid->tiles) {
            tile.agent_count = 0;
        }

        // Count agents per tile (skip agents being serviced -- they're
        // "inside")
        auto agents = EntityQuery()
                          .whereHasComponent<Agent>()
                          .whereHasComponent<Transform>()
                          .gen();
        for (Entity& agent : agents) {
            if (!agent.is_missing<BeingServiced>()) continue;
            auto& tf = agent.get<Transform>();
            auto [gx, gz] = grid->world_to_grid(tf.position.x, tf.position.y);
            if (grid->in_bounds(gx, gz)) {
                grid->at(gx, gz).agent_count++;
            }
        }
    }
};

// Apply crush damage to agents on critically dense tiles
struct CrushDamageSystem : System<> {
    void once(float dt) override {
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;

        // Iterate tiles, find critically dense ones
        for (int z = 0; z < MAP_SIZE; z++) {
            for (int x = 0; x < MAP_SIZE; x++) {
                const Tile& tile = grid->at(x, z);
                float density =
                    tile.agent_count / static_cast<float>(MAX_AGENTS_PER_TILE);
                if (density < DENSITY_CRITICAL) continue;

                // Find all agents on this tile and apply damage
                auto agents = EntityQuery()
                                  .whereHasComponent<Agent>()
                                  .whereHasComponent<Transform>()
                                  .whereHasComponent<AgentHealth>()
                                  .gen();
                for (Entity& agent_ent : agents) {
                    if (!agent_ent.is_missing<BeingServiced>()) continue;
                    auto& tf = agent_ent.get<Transform>();
                    auto [gx, gz] =
                        grid->world_to_grid(tf.position.x, tf.position.y);
                    if (gx == x && gz == z) {
                        agent_ent.get<AgentHealth>().hp -=
                            CRUSH_DAMAGE_RATE * dt;
                    }
                }
            }
        }
    }
};

// Remove dead agents and track death count
struct AgentDeathSystem : System<Agent, AgentHealth, Transform> {
    void for_each_with(Entity& e, Agent&, AgentHealth& health, Transform& tf,
                       float) override {
        if (health.hp > 0.f) return;

        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();

        int gx = -1, gz = -1;
        if (grid) {
            auto [px, pz] = grid->world_to_grid(tf.position.x, tf.position.y);
            gx = px;
            gz = pz;
        }

        if (gs) {
            gs->death_count++;
            log_info("Agent died at ({}, {}), deaths: {}/{}", gx, gz,
                     gs->death_count, gs->max_deaths);
        }

        e.cleanup = true;
    }
};

void register_update_systems(SystemManager& sm) {
    sm.register_update_system(std::make_unique<CameraInputSystem>());
    sm.register_update_system(std::make_unique<PathBuildSystem>());
    sm.register_update_system(std::make_unique<NeedTickSystem>());
    sm.register_update_system(std::make_unique<UpdateAgentGoalSystem>());
    sm.register_update_system(std::make_unique<AgentMovementSystem>());
    sm.register_update_system(std::make_unique<StageWatchingSystem>());
    sm.register_update_system(std::make_unique<FacilityServiceSystem>());
    sm.register_update_system(std::make_unique<SpawnAgentSystem>());
    sm.register_update_system(std::make_unique<UpdateTileDensitySystem>());
    sm.register_update_system(std::make_unique<CrushDamageSystem>());
    sm.register_update_system(std::make_unique<AgentDeathSystem>());
}
