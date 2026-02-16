// AFTER_HOURS_REPLACE_LOGGING and log.h must come first
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "afterhours/src/core/entity_query.h"
#include "components.h"
#include "engine/random_engine.h"
#include "entity_makers.h"
#include "systems.h"

// Helper: check if game is over (skip game logic)
static bool game_is_over() {
    auto* gs = EntityHelper::get_singleton_cmp<GameState>();
    return gs && gs->is_game_over();
}

// Helper: check if game is paused
static bool game_is_paused() {
    auto* clock = EntityHelper::get_singleton_cmp<GameClock>();
    return clock && clock->speed == GameSpeed::Paused;
}

// Helper: check if game logic should be skipped (paused or game over)
static bool skip_game_logic() { return game_is_over() || game_is_paused(); }

struct CameraInputSystem : System<ProvidesCamera> {
    void for_each_with(Entity&, ProvidesCamera& cam, float dt) override {
        cam.cam.handle_input(dt);
    }
};

// Advance game clock and detect phase changes
struct UpdateGameClockSystem : System<> {
    GameClock::Phase prev_phase = GameClock::Phase::Day;
    bool was_pause_down = false;

    void once(float dt) override {
        auto* clock = EntityHelper::get_singleton_cmp<GameClock>();
        if (!clock) return;

        // Toggle pause with SPACE (only when game is not over)
        if (!game_is_over()) {
            bool pause_down = action_down(InputAction::TogglePause);
            if (pause_down && !was_pause_down) {
                if (clock->speed == GameSpeed::Paused)
                    clock->speed = GameSpeed::OneX;
                else
                    clock->speed = GameSpeed::Paused;
                log_info("Game speed: {}",
                         clock->speed == GameSpeed::Paused ? "PAUSED" : "1x");
            }
            was_pause_down = pause_down;
        }

        // Advance time
        float game_dt = (dt / GameClock::SECONDS_PER_GAME_MINUTE) *
                        clock->speed_multiplier();
        clock->game_time_minutes += game_dt;

        // Wrap at 24 hours
        if (clock->game_time_minutes >= 1440.0f) {
            clock->game_time_minutes -= 1440.0f;
        }

        // Detect phase change
        GameClock::Phase new_phase = clock->get_phase();
        if (new_phase != prev_phase) {
            log_info("Phase: {} -> {}", GameClock::phase_name(prev_phase),
                     GameClock::phase_name(new_phase));
            prev_phase = new_phase;
        }
    }
};

// Handle path drawing (rectangle drag) and demolish mode
struct PathBuildSystem : System<> {
    void once(float) override {
        if (game_is_over()) return;
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
                        if (!grid->in_bounds(x, z)) continue;
                        // Only place path on grass — skip facilities, fences,
                        // etc.
                        if (grid->at(x, z).type != TileType::Grass) continue;
                        grid->at(x, z).type = TileType::Path;
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
        // Don't move into a full tile — stay put instead
        if (grid.at(nx, nz).agent_count >= MAX_AGENTS_PER_TILE) continue;

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

// When density is dangerous, randomly pick a less-crowded walkable neighbor.
// Uses weighted random so agents scatter in different directions.
static std::pair<int, int> pick_flee_tile(int cx, int cz, const Grid& grid) {
    int cur_count = grid.at(cx, cz).agent_count;

    struct Candidate {
        int x, z, count;
    };
    std::array<Candidate, 4> candidates;
    int n = 0;

    constexpr int dirs[][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (auto [dx, dz] : dirs) {
        int nx = cx + dx;
        int nz = cz + dz;
        if (!grid.in_bounds(nx, nz)) continue;
        TileType type = grid.at(nx, nz).type;
        if (type == TileType::Fence || type == TileType::Stage) continue;
        int count = grid.at(nx, nz).agent_count;
        if (count < cur_count) {
            candidates[n++] = {nx, nz, count};
        }
    }

    if (n == 0) return {cx, cz};
    if (n == 1) return {candidates[0].x, candidates[0].z};

    // Weight by how much emptier the neighbor is (higher weight = emptier)
    std::array<float, 4> weights;
    float total = 0.f;
    for (int i = 0; i < n; i++) {
        weights[i] = static_cast<float>(cur_count - candidates[i].count);
        total += weights[i];
    }

    float roll = RandomEngine::get().get_float(0.f, total);
    float accum = 0.f;
    for (int i = 0; i < n; i++) {
        accum += weights[i];
        if (roll <= accum) {
            return {candidates[i].x, candidates[i].z};
        }
    }
    return {candidates[n - 1].x, candidates[n - 1].z};
}

// Move agents toward their target using greedy pathfinding
struct AgentMovementSystem : System<Agent, Transform> {
    void for_each_with(Entity& e, Agent& agent, Transform& tf,
                       float dt) override {
        if (skip_game_logic()) return;
        if (!e.is_missing<BeingServiced>()) return;

        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();

        auto [cur_gx, cur_gz] =
            grid->world_to_grid(tf.position.x, tf.position.y);

        // Check if this tile is dangerously crowded — flee overrides everything
        bool fleeing = false;
        int next_x = cur_gx, next_z = cur_gz;
        if (grid->in_bounds(cur_gx, cur_gz)) {
            float density = grid->at(cur_gx, cur_gz).agent_count /
                            static_cast<float>(MAX_AGENTS_PER_TILE);
            if (density >= DENSITY_DANGEROUS) {
                // Already committed to a flee direction and not there yet?
                if (agent.flee_target_x >= 0 &&
                    (cur_gx != agent.flee_target_x ||
                     cur_gz != agent.flee_target_z)) {
                    next_x = agent.flee_target_x;
                    next_z = agent.flee_target_z;
                    fleeing = true;
                } else {
                    // Pick a new flee direction
                    auto [fx, fz] = pick_flee_tile(cur_gx, cur_gz, *grid);
                    if (fx != cur_gx || fz != cur_gz) {
                        next_x = fx;
                        next_z = fz;
                        agent.flee_target_x = fx;
                        agent.flee_target_z = fz;
                        fleeing = true;
                    }
                }
                if (fleeing) {
                    if (!e.is_missing<WatchingStage>()) {
                        e.removeComponent<WatchingStage>();
                    }
                    agent.speed = SPEED_PATH;
                    if (gs) agent.speed *= gs->speed_multiplier;
                }
            } else {
                // Safe — clear any flee commitment
                agent.flee_target_x = -1;
                agent.flee_target_z = -1;
            }
        }

        if (!fleeing) {
            // Normal movement — skip if watching stage
            if (!e.is_missing<WatchingStage>()) return;
            if (agent.target_grid_x < 0 || agent.target_grid_z < 0) return;
            if (cur_gx == agent.target_grid_x && cur_gz == agent.target_grid_z)
                return;

            TileType cur_type = TileType::Grass;
            if (grid->in_bounds(cur_gx, cur_gz)) {
                cur_type = grid->at(cur_gx, cur_gz).type;
            }
            agent.speed =
                (cur_type == TileType::Path || cur_type == TileType::Gate)
                    ? SPEED_PATH
                    : SPEED_GRASS;

            if (grid->in_bounds(cur_gx, cur_gz)) {
                float density = grid->at(cur_gx, cur_gz).agent_count /
                                static_cast<float>(MAX_AGENTS_PER_TILE);
                agent.speed *= density_speed_modifier(density);
            }
            if (gs) agent.speed *= gs->speed_multiplier;

            auto [px, pz] = pick_next_tile(cur_gx, cur_gz, agent.target_grid_x,
                                           agent.target_grid_z, *grid);
            next_x = px;
            next_z = pz;
        }

        // Move toward center of next tile
        ::vec2 target_world = grid->grid_to_world(next_x, next_z);
        float dx = target_world.x - tf.position.x;
        float dz = target_world.y - tf.position.y;
        float dist = std::sqrt(dx * dx + dz * dz);

        if (dist > 0.01f) {
            float step = agent.speed * TILESIZE * dt;
            if (step > dist) step = dist;
            tf.position.x += (dx / dist) * step;
            tf.position.y += (dz / dist) * step;
        }
    }
};

// Pick a random spot on the stage floor, biased toward the stage itself.
// Returns a grid position within the watch radius.
static std::pair<int, int> random_stage_spot() {
    auto& rng = RandomEngine::get();
    float scx = STAGE_X + STAGE_SIZE / 2.0f;
    float scz = STAGE_Z + STAGE_SIZE / 2.0f;

    // Bias: 60% chance to pick a spot within half the radius (close to stage)
    float max_r = STAGE_WATCH_RADIUS;
    if (rng.get_float(0.f, 1.f) < 0.6f) {
        max_r = STAGE_WATCH_RADIUS * 0.5f;
    }

    // Random angle and radius
    float angle = rng.get_float(0.f, 2.f * (float) M_PI);
    float r = rng.get_float(0.f, max_r);
    int gx = static_cast<int>(std::round(scx + r * std::cos(angle)));
    int gz = static_cast<int>(std::round(scz + r * std::sin(angle)));

    // Clamp to playable area
    gx = std::clamp(gx, PLAY_MIN, PLAY_MAX);
    gz = std::clamp(gz, PLAY_MIN, PLAY_MAX);
    return {gx, gz};
}

// Spawn agents at the spawn point on a timer
struct SpawnAgentSystem : System<> {
    void once(float dt) override {
        if (skip_game_logic()) return;
        auto* ss = EntityHelper::get_singleton_cmp<SpawnState>();
        if (!ss || !ss->enabled) return;

        ss->timer += dt;
        if (ss->timer >= ss->interval) {
            ss->timer -= ss->interval;

            // Spawn agent heading toward a random spot near the stage
            auto [sx, sz] = random_stage_spot();
            make_agent(SPAWN_X, SPAWN_Z, FacilityType::Stage, sx, sz);
        }
    }
};

// Check if a facility tile is "full" (too many agents)
static bool facility_is_full(int gx, int gz, const Grid& grid) {
    if (!grid.in_bounds(gx, gz)) return true;
    return grid.at(gx, gz).agent_count >= FACILITY_MAX_AGENTS;
}

// Find facilities of a given type sorted by distance, skipping full ones.
// Returns the closest non-full facility, or the closest full one if all are
// full (so agents still move toward it for urgent needs like bathroom).
static std::pair<int, int> find_nearest_facility(int from_x, int from_z,
                                                 TileType type,
                                                 const Grid& grid,
                                                 bool urgent = false) {
    struct FacEntry {
        int x, z, dist;
        bool full;
    };
    std::vector<FacEntry> found;

    // Collect unique facility anchor tiles (top-left of each footprint)
    // To avoid counting the same 2x2 facility 4 times, track seen anchors
    std::set<std::pair<int, int>> seen;
    for (int z = 0; z < MAP_SIZE; z++) {
        for (int x = 0; x < MAP_SIZE; x++) {
            if (grid.at(x, z).type != type) continue;
            // Use this tile as a candidate (dedup via distance later)
            auto key = std::make_pair(x, z);
            if (seen.count(key)) continue;
            seen.insert(key);

            int dist = std::abs(x - from_x) + std::abs(z - from_z);
            bool full = facility_is_full(x, z, grid);
            found.push_back({x, z, dist, full});
        }
    }

    // Sort by distance
    std::sort(
        found.begin(), found.end(),
        [](const FacEntry& a, const FacEntry& b) { return a.dist < b.dist; });

    // Try up to 3 closest non-full facilities
    for (int i = 0; i < std::min((int) found.size(), 3); i++) {
        if (!found[i].full) {
            return {found[i].x, found[i].z};
        }
    }

    // All full: urgent needs keep heading to closest, non-urgent gives up
    if (urgent && !found.empty()) {
        return {found[0].x, found[0].z};
    }
    if (!found.empty()) {
        return {-1, -1};  // non-urgent: give up
    }
    return {-1, -1};
}

// Tick need timers and set need flags
struct NeedTickSystem : System<Agent, AgentNeeds> {
    void for_each_with(Entity& e, Agent&, AgentNeeds& needs,
                       float dt) override {
        if (skip_game_logic()) return;
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
        if (skip_game_logic()) return;
        // Don't change goal while being serviced or watching
        if (!e.is_missing<BeingServiced>()) return;
        if (!e.is_missing<WatchingStage>()) return;

        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;

        auto [cur_gx, cur_gz] =
            grid->world_to_grid(tf.position.x, tf.position.y);

        if (needs.needs_bathroom) {
            // Bathroom is urgent — keep searching even if all are full
            auto [bx, bz] = find_nearest_facility(
                cur_gx, cur_gz, TileType::Bathroom, *grid, true);
            if (bx >= 0) {
                agent.want = FacilityType::Bathroom;
                agent.target_grid_x = bx;
                agent.target_grid_z = bz;
            }
        } else if (needs.needs_food) {
            // Food is non-urgent — give up if all are full
            auto [fx, fz] = find_nearest_facility(cur_gx, cur_gz,
                                                  TileType::Food, *grid, false);
            if (fx >= 0) {
                agent.want = FacilityType::Food;
                agent.target_grid_x = fx;
                agent.target_grid_z = fz;
            } else {
                // All food facilities full — go back to stage
                needs.needs_food = false;
                needs.food_timer = 0.f;
                agent.want = FacilityType::Stage;
                auto [rsx, rsz] = random_stage_spot();
                agent.target_grid_x = rsx;
                agent.target_grid_z = rsz;
            }
        } else {
            // Default: go to stage
            if (agent.want != FacilityType::Stage) {
                agent.want = FacilityType::Stage;
                auto [rsx, rsz] = random_stage_spot();
                agent.target_grid_x = rsx;
                agent.target_grid_z = rsz;
            }
        }
    }
};

// When agent reaches the stage area, start watching
struct StageWatchingSystem : System<Agent, Transform> {
    void for_each_with(Entity& e, Agent& agent, Transform& tf,
                       float dt) override {
        if (skip_game_logic()) return;
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
        if (skip_game_logic()) return;
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;

        // Already being serviced? Tick the timer.
        if (!e.is_missing<BeingServiced>()) {
            auto& bs = e.get<BeingServiced>();
            bs.time_remaining -= dt;
            if (bs.time_remaining <= 0.f) {
                // Service complete - clear need, reset timer
                auto* gs = EntityHelper::get_singleton_cmp<GameState>();
                if (gs) gs->total_agents_served++;
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

                // Reset goal to stage (random spot near it)
                agent.want = FacilityType::Stage;
                auto [rsx, rsz] = random_stage_spot();
                agent.target_grid_x = rsx;
                agent.target_grid_z = rsz;
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
        if (skip_game_logic()) return;
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
        if (skip_game_logic()) return;
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

// Helper: spawn death particles at a world position
static void spawn_death_particles(float wx, float wz, int count, float radius) {
    auto& rng = RandomEngine::get();
    for (int i = 0; i < count; i++) {
        Entity& pe = EntityHelper::createEntity();
        pe.addComponent<Transform>(::vec2{wx, wz});
        pe.addComponent<Particle>();
        auto& p = pe.get<Particle>();

        // Random velocity in a burst pattern
        float angle = rng.get_float(0.f, 6.283f);
        float speed = rng.get_float(radius * 0.5f, radius);
        p.velocity = {std::cos(angle) * speed, std::sin(angle) * speed};
        p.lifetime = rng.get_float(0.3f, 0.5f);
        p.max_lifetime = p.lifetime;
        p.size = rng.get_float(2.f, 4.f);

        // Red/white color mix
        if (rng.get_float(0.f, 1.f) > 0.5f) {
            p.color = {255, 80, 60, 255};
        } else {
            p.color = {255, 220, 200, 255};
        }
    }
}

// Remove dead agents, track death count, spawn death particles
struct AgentDeathSystem : System<> {
    void once(float) override {
        if (skip_game_logic()) return;
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();

        // Collect deaths per tile for mass merge
        struct DeathInfo {
            float wx = 0, wz = 0;
            int count = 0;
        };
        std::unordered_map<int, DeathInfo> deaths_per_tile;

        auto agents = EntityQuery()
                          .whereHasComponent<Agent>()
                          .whereHasComponent<AgentHealth>()
                          .whereHasComponent<Transform>()
                          .gen();
        for (Entity& e : agents) {
            auto& health = e.get<AgentHealth>();
            if (health.hp > 0.f) continue;

            auto& tf = e.get<Transform>();
            int gx = -1, gz = -1;
            if (grid) {
                auto [px, pz] =
                    grid->world_to_grid(tf.position.x, tf.position.y);
                gx = px;
                gz = pz;
            }

            if (gs) {
                gs->death_count++;
                log_info("Agent died at ({}, {}), deaths: {}/{}", gx, gz,
                         gs->death_count, gs->max_deaths);
            }

            // Track for particle merge
            int tile_key = gz * MAP_SIZE + gx;
            auto& info = deaths_per_tile[tile_key];
            info.wx = tf.position.x;
            info.wz = tf.position.y;
            info.count++;

            e.cleanup = true;
        }

        // Spawn particles per tile (merged for mass deaths)
        for (auto& [key, info] : deaths_per_tile) {
            if (info.count >= 5) {
                // Mass death: bigger burst
                spawn_death_particles(info.wx, info.wz, 12, 1.5f);
            } else {
                // Normal: small burst per death
                spawn_death_particles(info.wx, info.wz, 6 * info.count, 0.8f);
            }
        }
    }
};

// Toggle data layer overlay with TAB, debug panel with backtick.
// Uses manual edge detection (action_down + previous state) to ignore
// OS key-repeat events and E2E double-injection.
struct ToggleDataLayerSystem : System<> {
    bool was_data_layer_down = false;
    bool was_debug_down = false;

    void once(float) override {
        bool data_down = action_down(InputAction::ToggleDataLayer);
        if (data_down && !was_data_layer_down) {
            auto* gs = EntityHelper::get_singleton_cmp<GameState>();
            if (gs) {
                gs->show_data_layer = !gs->show_data_layer;
                log_info("Data layer: {}", gs->show_data_layer ? "ON" : "OFF");
            }
        }
        was_data_layer_down = data_down;

        bool debug_down = action_down(InputAction::ToggleUIDebug);
        if (debug_down && !was_debug_down) {
            auto* gs = EntityHelper::get_singleton_cmp<GameState>();
            if (gs) {
                gs->show_debug = !gs->show_debug;
                log_info("Debug panel: {}", gs->show_debug ? "ON" : "OFF");
            }
        }
        was_debug_down = debug_down;
    }
};

// Move particles and fade alpha; remove when lifetime expires
struct UpdateParticlesSystem : System<Particle, Transform> {
    void for_each_with(Entity& e, Particle& p, Transform& tf,
                       float dt) override {
        if (game_is_paused()) return;
        p.lifetime -= dt;
        if (p.lifetime <= 0.f) {
            e.cleanup = true;
            return;
        }
        tf.position.x += p.velocity.x * dt;
        tf.position.y += p.velocity.y * dt;
        // Fade alpha linearly
        float t = p.lifetime / p.max_lifetime;
        p.color.a = static_cast<unsigned char>(t * 255.f);
    }
};

// Track time_survived and max_attendees; check for game over
struct TrackStatsSystem : System<> {
    void once(float dt) override {
        if (skip_game_logic()) return;
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        if (!gs) return;

        gs->time_survived += dt;

        int count = static_cast<int>(
            EntityQuery().whereHasComponent<Agent>().gen_count());
        if (count > gs->max_attendees) {
            gs->max_attendees = count;
        }
    }
};

// Check if death count has reached max -> game over
struct CheckGameOverSystem : System<> {
    void once(float) override {
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        if (!gs || gs->is_game_over()) return;

        if (gs->death_count >= gs->max_deaths) {
            gs->status = GameStatus::GameOver;
            log_info("GAME OVER: {} deaths reached", gs->death_count);
        }
    }
};

// SPACE restarts the game when in game over state
struct RestartGameSystem : System<> {
    void once(float) override {
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        if (!gs || !gs->is_game_over()) return;

        if (action_pressed(InputAction::Restart)) {
            log_info("Restarting game...");

            // Clear all agents
            auto agents = EntityQuery().whereHasComponent<Agent>().gen();
            for (Entity& agent : agents) {
                agent.cleanup = true;
            }
            // Clear all particles
            auto particles = EntityQuery().whereHasComponent<Particle>().gen();
            for (Entity& p : particles) {
                p.cleanup = true;
            }
            EntityHelper::cleanup();

            // Reset grid
            auto* grid = EntityHelper::get_singleton_cmp<Grid>();
            if (grid) {
                for (auto& tile : grid->tiles) {
                    tile.type = TileType::Grass;
                    tile.agent_count = 0;
                }
                grid->init_perimeter();
            }

            // Reset game state
            gs->status = GameStatus::Running;
            gs->game_time = 0.f;
            gs->death_count = 0;
            gs->total_agents_served = 0;
            gs->time_survived = 0.f;
            gs->max_attendees = 0;
            gs->show_data_layer = false;

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
        }
    }
};

void register_update_systems(SystemManager& sm) {
    sm.register_update_system(std::make_unique<CameraInputSystem>());
    sm.register_update_system(std::make_unique<UpdateGameClockSystem>());
    sm.register_update_system(std::make_unique<PathBuildSystem>());
    sm.register_update_system(std::make_unique<ToggleDataLayerSystem>());
    sm.register_update_system(std::make_unique<NeedTickSystem>());
    sm.register_update_system(std::make_unique<UpdateAgentGoalSystem>());
    sm.register_update_system(std::make_unique<SpawnAgentSystem>());
    sm.register_update_system(std::make_unique<UpdateTileDensitySystem>());
    sm.register_update_system(std::make_unique<AgentMovementSystem>());
    sm.register_update_system(std::make_unique<StageWatchingSystem>());
    sm.register_update_system(std::make_unique<FacilityServiceSystem>());
    sm.register_update_system(std::make_unique<CrushDamageSystem>());
    sm.register_update_system(std::make_unique<AgentDeathSystem>());
    sm.register_update_system(std::make_unique<TrackStatsSystem>());
    sm.register_update_system(std::make_unique<CheckGameOverSystem>());
    sm.register_update_system(std::make_unique<RestartGameSystem>());
    sm.register_update_system(std::make_unique<UpdateParticlesSystem>());
}
