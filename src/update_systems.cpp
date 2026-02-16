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

// Generate a scheduled artist at a given start time
static ScheduledArtist generate_artist(float start_minutes, int max_att) {
    auto& rng = RandomEngine::get();
    ScheduledArtist a;
    a.name = fmt::format("Artist {:03d}", rng.get_int(100, 999));
    a.start_time_minutes = start_minutes;
    a.duration_minutes = rng.get_float(30.f, 60.f);
    int base = 50 + (max_att / 10);
    int variation = static_cast<int>(base * 0.3f);
    a.expected_crowd = base + rng.get_int(-variation, variation);
    if (a.expected_crowd < 20) a.expected_crowd = 20;
    return a;
}

// Fill schedule with look-ahead artists starting from given time
static void fill_schedule(ArtistSchedule& sched, float after_time,
                          int max_att) {
    // Fixed 2-hour slots: 10:00, 12:00, 14:00, 16:00, 18:00, 20:00, 22:00
    static constexpr float slots[] = {600, 720, 840, 960, 1080, 1200, 1320};
    static constexpr int NUM_SLOTS = 7;

    while ((int) sched.schedule.size() < sched.look_ahead) {
        // Find next available slot after the latest scheduled artist
        float last_end = after_time;
        if (!sched.schedule.empty()) {
            auto& back = sched.schedule.back();
            last_end = back.start_time_minutes + back.duration_minutes + 30.f;
        }
        // Find the next slot time
        float next_slot = -1;
        for (int i = 0; i < NUM_SLOTS; i++) {
            if (slots[i] >= last_end) {
                next_slot = slots[i];
                break;
            }
        }
        if (next_slot < 0) {
            // Wrap to next day: add 1440 to first slot
            next_slot = slots[0] + 1440.f;
        }
        sched.schedule.push_back(generate_artist(next_slot, max_att));
    }
}

// Update artist schedule: advance states, manage stage state machine
struct UpdateArtistScheduleSystem : System<> {
    bool initialized = false;

    void once(float) override {
        if (skip_game_logic()) return;
        auto* sched = EntityHelper::get_singleton_cmp<ArtistSchedule>();
        auto* clock = EntityHelper::get_singleton_cmp<GameClock>();
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        if (!sched || !clock || !gs) return;

        float now = clock->game_time_minutes;

        // Initialize schedule on first run
        if (!initialized) {
            fill_schedule(*sched, now, gs->max_attendees);
            initialized = true;
        }

        // Update artist states
        sched->stage_state = StageState::Idle;
        sched->current_artist_idx = -1;

        for (int i = 0; i < (int) sched->schedule.size(); i++) {
            auto& a = sched->schedule[i];
            if (a.finished) continue;

            float end_time = a.start_time_minutes + a.duration_minutes;
            float announce_time = a.start_time_minutes - 15.f;

            if (now >= end_time) {
                // Performance ended
                if (a.performing) {
                    a.performing = false;
                    a.finished = true;
                    sched->stage_state = StageState::Clearing;
                    log_info("Artist '{}' finished", a.name);
                }
            } else if (now >= a.start_time_minutes) {
                // Performing
                if (!a.performing) {
                    a.performing = true;
                    a.announced = true;
                    log_info("Artist '{}' now performing (crowd ~{})", a.name,
                             a.expected_crowd);
                }
                sched->stage_state = StageState::Performing;
                sched->current_artist_idx = i;
                break;
            } else if (now >= announce_time) {
                // Announcing
                if (!a.announced) {
                    a.announced = true;
                    log_info("Announcing: '{}' at {}", a.name,
                             fmt::format("{:02d}:{:02d}",
                                         (int) (a.start_time_minutes / 60) % 24,
                                         (int) a.start_time_minutes % 60));
                }
                sched->stage_state = StageState::Announcing;
                break;
            } else {
                break;  // Future artists not yet relevant
            }
        }

        // Prune finished artists and refill
        while (!sched->schedule.empty() && sched->schedule.front().finished) {
            sched->schedule.erase(sched->schedule.begin());
            if (sched->current_artist_idx > 0) sched->current_artist_idx--;
        }
        if ((int) sched->schedule.size() < sched->look_ahead) {
            fill_schedule(*sched, now, gs->max_attendees);
        }

        // Adjust spawn rate based on artist schedule
        auto* ss = EntityHelper::get_singleton_cmp<SpawnState>();
        if (ss) {
            float base_rate = 1.f / DEFAULT_SPAWN_INTERVAL;
            auto* current = sched->get_current();
            auto* next = sched->get_next();

            if (current) {
                float mult = current->expected_crowd / 100.0f;
                if (mult < 0.5f) mult = 0.5f;
                ss->interval = 1.f / (base_rate * mult);
            } else if (next && now > next->start_time_minutes - 15.f) {
                float mult = next->expected_crowd / 100.0f;
                if (mult < 0.5f) mult = 0.5f;
                ss->interval = 1.f / (base_rate * mult);
            } else {
                ss->interval = DEFAULT_SPAWN_INTERVAL;
            }
        }
    }
};

// Check if all tiles in a footprint are valid for placement
static bool can_place_at(const Grid& grid, int x, int z, int w, int h) {
    for (int dz = 0; dz < h; dz++) {
        for (int dx = 0; dx < w; dx++) {
            int tx = x + dx, tz = z + dz;
            if (!grid.in_bounds(tx, tz)) return false;
            if (tx < PLAY_MIN || tx > PLAY_MAX) return false;
            if (tz < PLAY_MIN || tz > PLAY_MAX) return false;
            TileType t = grid.at(tx, tz).type;
            if (t != TileType::Grass && t != TileType::Path &&
                t != TileType::StageFloor)
                return false;
        }
    }
    return true;
}

// Count gates on the map
static int count_gates(const Grid& grid) {
    int count = 0;
    for (int z = 0; z < MAP_SIZE; z++)
        for (int x = 0; x < MAP_SIZE; x++)
            if (grid.at(x, z).type == TileType::Gate) count++;
    return count / 2;  // gates are 2x1
}

// Handle all build tools: rect drag for path/fence, point for facilities
struct PathBuildSystem : System<> {
    void once(float) override {
        if (game_is_over()) return;
        auto* pds = EntityHelper::get_singleton_cmp<PathDrawState>();
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        auto* bs = EntityHelper::get_singleton_cmp<BuilderState>();
        if (!pds || !grid || !bs) return;

        // Tool cycling with [ ]
        if (action_pressed(InputAction::PrevTool)) {
            int t = static_cast<int>(bs->tool);
            t = (t + 6) % 7;  // wrap backwards through 7 tools
            bs->tool = static_cast<BuildTool>(t);
            pds->is_drawing = false;
        }
        if (action_pressed(InputAction::NextTool)) {
            int t = static_cast<int>(bs->tool);
            t = (t + 1) % 7;
            bs->tool = static_cast<BuildTool>(t);
            pds->is_drawing = false;
        }
        // Direct select
        if (action_pressed(InputAction::ToolPath)) bs->tool = BuildTool::Path;
        if (action_pressed(InputAction::ToolFence)) bs->tool = BuildTool::Fence;
        if (action_pressed(InputAction::ToolGate)) bs->tool = BuildTool::Gate;
        if (action_pressed(InputAction::ToolStage)) bs->tool = BuildTool::Stage;
        if (action_pressed(InputAction::Tool5)) bs->tool = BuildTool::Bathroom;
        if (action_pressed(InputAction::Tool6)) bs->tool = BuildTool::Food;
        if (action_pressed(InputAction::Tool7)) bs->tool = BuildTool::Demolish;
        if (action_pressed(InputAction::ToggleDemolish))
            bs->tool = BuildTool::Demolish;

        // Cancel with Escape or right-click
        if (action_pressed(InputAction::Cancel) ||
            input::is_mouse_button_pressed(raylib::MOUSE_BUTTON_RIGHT)) {
            pds->is_drawing = false;
            return;
        }

        if (!pds->hover_valid) return;

        // Left-click actions
        if (input::is_mouse_button_pressed(raylib::MOUSE_BUTTON_LEFT)) {
            int hx = pds->hover_x, hz = pds->hover_z;

            switch (bs->tool) {
                case BuildTool::Path:
                case BuildTool::Fence: {
                    if (!pds->is_drawing) {
                        pds->start_x = hx;
                        pds->start_z = hz;
                        pds->is_drawing = true;
                    } else {
                        int min_x, min_z, max_x, max_z;
                        pds->get_rect(min_x, min_z, max_x, max_z);
                        TileType fill = (bs->tool == BuildTool::Path)
                                            ? TileType::Path
                                            : TileType::Fence;
                        for (int z = min_z; z <= max_z; z++) {
                            for (int x = min_x; x <= max_x; x++) {
                                if (!grid->in_bounds(x, z)) continue;
                                if (grid->at(x, z).type != TileType::Grass)
                                    continue;
                                grid->at(x, z).type = fill;
                            }
                        }
                        pds->is_drawing = false;
                    }
                    break;
                }
                case BuildTool::Gate: {
                    if (can_place_at(*grid, hx, hz, 1, 2)) {
                        grid->at(hx, hz).type = TileType::Gate;
                        grid->at(hx, hz + 1).type = TileType::Gate;
                    }
                    break;
                }
                case BuildTool::Stage: {
                    if (can_place_at(*grid, hx, hz, 4, 4)) {
                        for (int dz = 0; dz < 4; dz++)
                            for (int dx = 0; dx < 4; dx++)
                                grid->at(hx + dx, hz + dz).type =
                                    TileType::Stage;
                    }
                    break;
                }
                case BuildTool::Bathroom: {
                    if (can_place_at(*grid, hx, hz, 2, 2)) {
                        for (int dz = 0; dz < 2; dz++)
                            for (int dx = 0; dx < 2; dx++)
                                grid->at(hx + dx, hz + dz).type =
                                    TileType::Bathroom;
                    }
                    break;
                }
                case BuildTool::Food: {
                    if (can_place_at(*grid, hx, hz, 2, 2)) {
                        for (int dz = 0; dz < 2; dz++)
                            for (int dx = 0; dx < 2; dx++)
                                grid->at(hx + dx, hz + dz).type =
                                    TileType::Food;
                    }
                    break;
                }
                case BuildTool::Demolish: {
                    Tile& tile = grid->at(hx, hz);
                    if (tile.type == TileType::Path ||
                        tile.type == TileType::Fence ||
                        tile.type == TileType::Bathroom ||
                        tile.type == TileType::Food ||
                        tile.type == TileType::Stage) {
                        // Don't demolish last gate
                        if (tile.type == TileType::Gate &&
                            count_gates(*grid) <= 1)
                            break;
                        tile.type = TileType::Grass;
                    }
                    break;
                }
            }
        }
    }
};

// Forward declaration for pheromone channel mapping
static int facility_to_channel(FacilityType type);

// Greedy neighbor pathfinding with pheromone weighting.
// Pheromone reduces effective distance score, creating emergent trails.
static std::pair<int, int> pick_next_tile(
    int cur_x, int cur_z, int goal_x, int goal_z, const Grid& grid,
    FacilityType want = FacilityType::Stage) {
    static constexpr int dx[] = {1, -1, 0, 0};
    static constexpr int dz[] = {0, 0, 1, -1};

    int best_x = cur_x;
    int best_z = cur_z;
    float best_score =
        static_cast<float>(std::abs(cur_x - goal_x) + std::abs(cur_z - goal_z));
    bool best_is_path = false;

    int channel = facility_to_channel(want);

    for (int i = 0; i < 4; i++) {
        int nx = cur_x + dx[i];
        int nz = cur_z + dz[i];
        if (!grid.in_bounds(nx, nz)) continue;

        TileType type = grid.at(nx, nz).type;
        if (type == TileType::Fence) continue;
        if (grid.at(nx, nz).agent_count >= MAX_AGENTS_PER_TILE) continue;

        float dist =
            static_cast<float>(std::abs(nx - goal_x) + std::abs(nz - goal_z));
        float phero = Tile::to_strength(grid.at(nx, nz).pheromone[channel]);
        float score = dist - (phero * 2.0f);

        bool is_path = (type == TileType::Path || type == TileType::Gate);

        if (score < best_score || (std::abs(score - best_score) < 0.01f &&
                                   is_path && !best_is_path)) {
            best_x = nx;
            best_z = nz;
            best_score = score;
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

            auto [px, pz] =
                pick_next_tile(cur_gx, cur_gz, agent.target_grid_x,
                               agent.target_grid_z, *grid, agent.want);
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

        // Don't spawn during Dead Hours (3am-10am)
        auto* clock = EntityHelper::get_singleton_cmp<GameClock>();
        if (clock && clock->get_phase() == GameClock::Phase::DeadHours) return;

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

                // Start depositing pheromone trail for the facility just
                // visited
                if (e.is_missing<PheromoneDepositor>()) {
                    e.addComponent<PheromoneDepositor>();
                }
                auto& pdep = e.get<PheromoneDepositor>();
                pdep.leaving_type = bs.facility_type;
                pdep.is_depositing = true;
                pdep.deposit_distance = 0.f;

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

// Map FacilityType to pheromone channel index
static int facility_to_channel(FacilityType type) {
    switch (type) {
        case FacilityType::Bathroom:
            return Tile::PHERO_BATHROOM;
        case FacilityType::Food:
            return Tile::PHERO_FOOD;
        case FacilityType::Stage:
            return Tile::PHERO_STAGE;
        case FacilityType::Exit:
            return Tile::PHERO_EXIT;
    }
    return Tile::PHERO_STAGE;
}

// During Exodus, flood exit pheromone from gates using BFS
static void flood_exit_pheromone(Grid& grid) {
    std::queue<std::pair<int, int>> frontier;
    for (int z = 0; z < MAP_SIZE; z++) {
        for (int x = 0; x < MAP_SIZE; x++) {
            if (grid.at(x, z).type == TileType::Gate) {
                grid.at(x, z).pheromone[Tile::PHERO_EXIT] = 255;
                frontier.push({x, z});
            }
        }
    }
    constexpr int dirs[][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    while (!frontier.empty()) {
        auto [x, z] = frontier.front();
        frontier.pop();
        uint8_t current = grid.at(x, z).pheromone[Tile::PHERO_EXIT];
        if (current <= 5) continue;
        for (auto [dx, dz] : dirs) {
            int nx = x + dx, nz = z + dz;
            if (!grid.in_bounds(nx, nz)) continue;
            TileType type = grid.at(nx, nz).type;
            if (type == TileType::Fence || type == TileType::Stage) continue;
            if (grid.at(nx, nz).pheromone[Tile::PHERO_EXIT] < current - 5) {
                grid.at(nx, nz).pheromone[Tile::PHERO_EXIT] = current - 5;
                frontier.push({nx, nz});
            }
        }
    }
}

// During Exodus phase: switch agents to exit mode and emit exit pheromone
struct ExodusSystem : System<> {
    bool flooded_this_exodus = false;
    GameClock::Phase prev_phase = GameClock::Phase::Day;

    void once(float) override {
        if (skip_game_logic()) return;
        auto* clock = EntityHelper::get_singleton_cmp<GameClock>();
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!clock || !grid) return;

        auto phase = clock->get_phase();

        // Detect entering Exodus phase
        if (phase == GameClock::Phase::Exodus &&
            prev_phase != GameClock::Phase::Exodus) {
            flooded_this_exodus = false;
            log_info("Exodus begins — gates emitting exit pheromone");
        }

        // Detect entering Dead Hours — mark carryover
        if (phase == GameClock::Phase::DeadHours &&
            prev_phase == GameClock::Phase::Exodus) {
            auto* gs = EntityHelper::get_singleton_cmp<GameState>();
            int count = 0;
            auto agents = EntityQuery().whereHasComponent<Agent>().gen();
            for (Entity& e : agents) {
                if (e.is_missing<CarryoverAgent>()) {
                    e.addComponent<CarryoverAgent>();
                    count++;
                }
            }
            if (gs) gs->carryover_count = count;
            if (count > 0) log_info("Carryover: {} agents stuck", count);
        }

        prev_phase = phase;

        if (phase != GameClock::Phase::Exodus) return;

        // Flood exit pheromone once at start of Exodus
        if (!flooded_this_exodus) {
            flood_exit_pheromone(*grid);
            flooded_this_exodus = true;
        }

        // Keep gate pheromone at max during Exodus
        for (int z = 0; z < MAP_SIZE; z++) {
            for (int x = 0; x < MAP_SIZE; x++) {
                if (grid->at(x, z).type == TileType::Gate) {
                    grid->at(x, z).pheromone[Tile::PHERO_EXIT] = 255;
                }
            }
        }

        // Switch all agents to Exit mode
        auto agents = EntityQuery().whereHasComponent<Agent>().gen();
        for (Entity& e : agents) {
            auto& agent = e.get<Agent>();
            if (agent.want != FacilityType::Exit) {
                agent.want = FacilityType::Exit;
                // Target the nearest gate
                agent.target_grid_x = GATE_X;
                agent.target_grid_z = GATE_Z1;
            }
        }
    }
};

// Remove agents that reach a gate during Exodus
struct GateExitSystem : System<Agent, Transform> {
    void for_each_with(Entity& e, Agent& agent, Transform& tf, float) override {
        if (skip_game_logic()) return;
        auto* clock = EntityHelper::get_singleton_cmp<GameClock>();
        if (!clock || clock->get_phase() != GameClock::Phase::Exodus) return;
        if (agent.want != FacilityType::Exit) return;

        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;

        auto [gx, gz] = grid->world_to_grid(tf.position.x, tf.position.y);
        if (!grid->in_bounds(gx, gz)) return;

        if (grid->at(gx, gz).type == TileType::Gate) {
            auto* gs = EntityHelper::get_singleton_cmp<GameState>();
            if (gs) gs->agents_exited++;
            e.cleanup = true;
        }
    }
};

// Deposit pheromones as agents walk away from facilities
struct PheromoneDepositSystem : System<Agent, Transform, PheromoneDepositor> {
    void for_each_with(Entity&, Agent&, Transform& tf, PheromoneDepositor& dep,
                       float) override {
        if (skip_game_logic()) return;
        if (!dep.is_depositing) return;
        if (dep.deposit_distance >= PheromoneDepositor::MAX_DEPOSIT_DISTANCE) {
            dep.is_depositing = false;
            return;
        }

        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;

        auto [gx, gz] = grid->world_to_grid(tf.position.x, tf.position.y);
        if (!grid->in_bounds(gx, gz)) return;

        int ch = facility_to_channel(dep.leaving_type);
        auto& val = grid->at(gx, gz).pheromone[ch];
        int nv = static_cast<int>(val) + 50;
        val = static_cast<uint8_t>(std::min(nv, 255));
        dep.deposit_distance += 1.0f;
    }
};

// Decay all pheromones periodically
struct DecayPheromonesSystem : System<> {
    int frame_counter = 0;

    void once(float) override {
        if (skip_game_logic()) return;
        frame_counter++;
        if (frame_counter % 90 != 0) return;  // ~1.5 sec at 60fps

        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;
        for (auto& tile : grid->tiles) {
            for (auto& p : tile.pheromone) {
                if (p > 0) p--;
            }
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

// Track time_survived and max_attendees; check for game over; progression
struct TrackStatsSystem : System<> {
    void once(float dt) override {
        if (skip_game_logic()) return;
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        if (!gs) return;

        gs->time_survived += dt;

        int count = static_cast<int>(
            EntityQuery().whereHasComponent<Agent>().gen_count());

        int old_max = gs->max_attendees;
        if (count > gs->max_attendees) {
            gs->max_attendees = count;
        }

        // Check for milestone unlock (every 100 max attendees)
        auto* fs = EntityHelper::get_singleton_cmp<FacilitySlots>();
        if (fs) {
            int old_slots = fs->get_slots_per_type(old_max);
            int new_slots = fs->get_slots_per_type(gs->max_attendees);
            if (new_slots > old_slots) {
                // Create toast notification
                Entity& te = EntityHelper::createEntity();
                te.addComponent<ToastMessage>();
                te.get<ToastMessage>().text = "New facility slots unlocked!";
                log_info("Progression: {} slots per type (max_attendees={})",
                         new_slots, gs->max_attendees);
            }
        }
    }
};

// Update toast messages and remove expired ones
struct UpdateToastsSystem : System<ToastMessage> {
    void for_each_with(Entity& e, ToastMessage& toast, float dt) override {
        toast.elapsed += dt;
        if (toast.elapsed >= toast.lifetime) {
            e.cleanup = true;
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

            // Reset artist schedule
            auto* sched = EntityHelper::get_singleton_cmp<ArtistSchedule>();
            if (sched) {
                sched->schedule.clear();
                sched->stage_state = StageState::Idle;
                sched->current_artist_idx = -1;
            }

            // Reset game state extras
            gs->agents_exited = 0;
            gs->carryover_count = 0;
        }
    }
};

void register_update_systems(SystemManager& sm) {
    sm.register_update_system(std::make_unique<CameraInputSystem>());
    sm.register_update_system(std::make_unique<UpdateGameClockSystem>());
    sm.register_update_system(std::make_unique<UpdateArtistScheduleSystem>());
    sm.register_update_system(std::make_unique<PathBuildSystem>());
    sm.register_update_system(std::make_unique<ToggleDataLayerSystem>());
    sm.register_update_system(std::make_unique<NeedTickSystem>());
    sm.register_update_system(std::make_unique<UpdateAgentGoalSystem>());
    sm.register_update_system(std::make_unique<SpawnAgentSystem>());
    sm.register_update_system(std::make_unique<ExodusSystem>());
    sm.register_update_system(std::make_unique<GateExitSystem>());
    sm.register_update_system(std::make_unique<PheromoneDepositSystem>());
    sm.register_update_system(std::make_unique<DecayPheromonesSystem>());
    sm.register_update_system(std::make_unique<UpdateTileDensitySystem>());
    sm.register_update_system(std::make_unique<AgentMovementSystem>());
    sm.register_update_system(std::make_unique<StageWatchingSystem>());
    sm.register_update_system(std::make_unique<FacilityServiceSystem>());
    sm.register_update_system(std::make_unique<CrushDamageSystem>());
    sm.register_update_system(std::make_unique<AgentDeathSystem>());
    sm.register_update_system(std::make_unique<TrackStatsSystem>());
    sm.register_update_system(std::make_unique<UpdateToastsSystem>());
    sm.register_update_system(std::make_unique<CheckGameOverSystem>());
    sm.register_update_system(std::make_unique<RestartGameSystem>());
    sm.register_update_system(std::make_unique<UpdateParticlesSystem>());
}
