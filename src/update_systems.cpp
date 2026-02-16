// AFTER_HOURS_REPLACE_LOGGING and log.h must come first
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "afterhours/src/core/entity_query.h"
#include "audio.h"
#include "components.h"
#include "engine/random_engine.h"
#include "entity_makers.h"
#include "save_system.h"
#include "systems.h"

// Global event effect flags set by ApplyEventEffectsSystem each frame.
// Read by AgentMovementSystem (rain) and NeedTickSystem (heat).
namespace event_flags {
static bool rain_active = false;
static bool heat_active = false;
}  // namespace event_flags

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

// Helper: create a toast notification entity with audio cue
static void spawn_toast(const std::string& text, float lifetime = 3.0f) {
    Entity& te = EntityHelper::createEntity();
    te.addComponent<ToastMessage>();
    auto& toast = te.get<ToastMessage>();
    toast.text = text;
    toast.lifetime = lifetime;
    get_audio().play_toast();
}

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

        // Adjust spawn rate based on artist schedule (skip if debug override)
        auto* ss = EntityHelper::get_singleton_cmp<SpawnState>();
        if (ss && !ss->manual_override) {
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
            if (!grid.in_playable(tx, tz)) return false;
            TileType t = grid.at(tx, tz).type;
            if (t != TileType::Grass && t != TileType::Path &&
                t != TileType::StageFloor)
                return false;
        }
    }
    return true;
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
            t = (t + 7) % 8;  // wrap backwards through 8 tools
            bs->tool = static_cast<BuildTool>(t);
            pds->is_drawing = false;
        }
        if (action_pressed(InputAction::NextTool)) {
            int t = static_cast<int>(bs->tool);
            t = (t + 1) % 8;
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
        if (action_pressed(InputAction::Tool7)) bs->tool = BuildTool::MedTent;
        if (action_pressed(InputAction::Tool8)) bs->tool = BuildTool::Demolish;
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
                        grid->mark_tiles_dirty();
                        get_audio().play_place();
                    }
                    break;
                }
                case BuildTool::Gate: {
                    if (can_place_at(*grid, hx, hz, 1, 2)) {
                        grid->place_footprint(hx, hz, 1, 2, TileType::Gate);
                        grid->rebuild_gate_cache();
                        get_audio().play_place();
                    }
                    break;
                }
                case BuildTool::Stage: {
                    if (can_place_at(*grid, hx, hz, 4, 4)) {
                        grid->place_footprint(hx, hz, 4, 4, TileType::Stage);
                        get_audio().play_place();
                    }
                    break;
                }
                case BuildTool::Bathroom: {
                    if (can_place_at(*grid, hx, hz, 2, 2)) {
                        grid->place_footprint(hx, hz, 2, 2, TileType::Bathroom);
                        get_audio().play_place();
                    }
                    break;
                }
                case BuildTool::Food: {
                    if (can_place_at(*grid, hx, hz, 2, 2)) {
                        grid->place_footprint(hx, hz, 2, 2, TileType::Food);
                        get_audio().play_place();
                    }
                    break;
                }
                case BuildTool::MedTent: {
                    if (can_place_at(*grid, hx, hz, 2, 2)) {
                        grid->place_footprint(hx, hz, 2, 2, TileType::MedTent);
                        get_audio().play_place();
                    }
                    break;
                }
                case BuildTool::Demolish: {
                    Tile& tile = grid->at(hx, hz);
                    if (tile.type == TileType::Path ||
                        tile.type == TileType::Fence ||
                        tile.type == TileType::Bathroom ||
                        tile.type == TileType::Food ||
                        tile.type == TileType::MedTent ||
                        tile.type == TileType::Stage) {
                        bool is_gate = (tile.type == TileType::Gate);
                        if (is_gate && grid->gate_count() <= 1) break;
                        tile.type = TileType::Grass;
                        grid->mark_tiles_dirty();
                        get_audio().play_demolish();
                    }
                    break;
                }
            }
        }
    }
};

// Forward declaration for pheromone channel mapping
static int facility_to_channel(FacilityType type);

// Lookup table: true if tile blocks agent movement (fences + buildings).
// Indexed by TileType enum: Grass=0,Path=1,Fence=2,Gate=3,Stage=4,
//   StageFloor=5,Bathroom=6,Food=7,MedTent=8
static constexpr bool TILE_BLOCKS[] = {
    false,  // Grass
    false,  // Path
    true,   // Fence
    false,  // Gate
    true,   // Stage
    false,  // StageFloor
    true,   // Bathroom
    true,   // Food
    true,   // MedTent
};

static bool tile_blocks_movement(TileType type) {
    return TILE_BLOCKS[static_cast<int>(type)];
}

// Greedy neighbor pathfinding with pheromone weighting.
// Pheromone reduces effective distance score, creating emergent trails.
// When stuck behind an obstacle, takes a random lateral step to route around.
static std::pair<int, int> pick_next_tile(
    int cur_x, int cur_z, int goal_x, int goal_z, const Grid& grid,
    FacilityType want = FacilityType::Stage) {
    static constexpr int dx[] = {1, -1, 0, 0};
    static constexpr int dz[] = {0, 0, 1, -1};

    int best_x = cur_x;
    int best_z = cur_z;
    float cur_dist =
        static_cast<float>(std::abs(cur_x - goal_x) + std::abs(cur_z - goal_z));
    float best_score = cur_dist;
    bool best_is_path = false;

    int channel = facility_to_channel(want);

    // Collect all walkable neighbors and their scores
    struct Neighbor {
        int x, z;
        float score, dist;
        bool is_path;
    };
    std::array<Neighbor, 4> walkable;
    int walkable_count = 0;

    for (int i = 0; i < 4; i++) {
        int nx = cur_x + dx[i];
        int nz = cur_z + dz[i];
        if (!grid.in_bounds(nx, nz)) continue;

        TileType type = grid.at(nx, nz).type;
        if (tile_blocks_movement(type)) continue;
        if (grid.at(nx, nz).agent_count >= MAX_AGENTS_PER_TILE) continue;

        float dist =
            static_cast<float>(std::abs(nx - goal_x) + std::abs(nz - goal_z));
        float phero = Tile::to_strength(grid.at(nx, nz).pheromone[channel]);
        float score = dist - (phero * 2.0f);

        bool is_path = (type == TileType::Path || type == TileType::Gate ||
                        type == TileType::StageFloor);

        walkable[walkable_count++] = {nx, nz, score, dist, is_path};

        if (score < best_score || (std::abs(score - best_score) < 0.01f &&
                                   is_path && !best_is_path)) {
            best_x = nx;
            best_z = nz;
            best_score = score;
            best_is_path = is_path;
        }
    }

    // If stuck (no improving neighbor found), take a random lateral step
    // to route around obstacles. Prefer tiles that don't increase distance
    // much, and randomize to avoid oscillation.
    if (best_x == cur_x && best_z == cur_z && walkable_count > 0) {
        // Shuffle walkable neighbors
        auto& rng = RandomEngine::get();
        for (int k = walkable_count - 1; k > 0; k--) {
            int j = rng.get_int(0, k);
            std::swap(walkable[k], walkable[j]);
        }
        // Pick the first neighbor that doesn't go too far from the goal
        for (int i = 0; i < walkable_count; i++) {
            if (walkable[i].dist <= cur_dist + 2.f) {
                return {walkable[i].x, walkable[i].z};
            }
        }
        // Last resort: any walkable neighbor
        return {walkable[0].x, walkable[0].z};
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
        if (tile_blocks_movement(type)) continue;
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
                    if (event_flags::rain_active) agent.speed *= 0.5f;
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
                (cur_type == TileType::Path || cur_type == TileType::Gate ||
                 cur_type == TileType::StageFloor)
                    ? SPEED_PATH
                    : SPEED_GRASS;

            if (grid->in_bounds(cur_gx, cur_gz)) {
                float density = grid->at(cur_gx, cur_gz).agent_count /
                                static_cast<float>(MAX_AGENTS_PER_TILE);
                agent.speed *= density_speed_modifier(density);
            }
            if (gs) agent.speed *= gs->speed_multiplier;
            if (event_flags::rain_active) agent.speed *= 0.5f;

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

// Pick a spot on the stage floor as close to the stage as possible
// The agent_count at which a tile is considered "orange" (crowded) on the
// density overlay, and agents should look for a different spot.
static constexpr int STAGE_CROWD_LIMIT =
    static_cast<int>(DENSITY_WARNING * MAX_AGENTS_PER_TILE);  // 20

// Pick the closest non-crowded StageFloor tile to the agent at (from_x,from_z).
// "Crowded" = agent_count >= STAGE_CROWD_LIMIT (the orange threshold).
// To prevent all agents from the same position targeting the exact same tile,
// we collect all non-crowded candidates within a small tolerance of the
// absolute closest distance, then pick randomly among them.
std::pair<int, int> best_stage_spot(int from_x, int from_z) {
    auto* grid = EntityHelper::get_singleton_cmp<Grid>();
    auto& rng = RandomEngine::get();
    float scx = STAGE_X + STAGE_SIZE / 2.0f;
    float scz = STAGE_Z + STAGE_SIZE / 2.0f;

    if (grid) {
        grid->ensure_caches();
        const auto& spots = grid->stage_floor_spots;

        if (!spots.empty()) {
            constexpr int DIST_TOLERANCE = 3;  // tiles of slack

            // First pass: find the minimum distance among non-crowded tiles
            int min_dist = INT_MAX;
            for (const auto& s : spots) {
                if (grid->at(s.x, s.z).agent_count < STAGE_CROWD_LIMIT) {
                    int dist = std::abs(s.x - from_x) + std::abs(s.z - from_z);
                    if (dist < min_dist) min_dist = dist;
                }
            }

            // Second pass: collect candidates within tolerance of best
            if (min_dist < INT_MAX) {
                struct Candidate {
                    int x, z;
                };
                std::vector<Candidate> near;
                near.reserve(16);
                int limit = min_dist + DIST_TOLERANCE;
                for (const auto& s : spots) {
                    if (grid->at(s.x, s.z).agent_count >= STAGE_CROWD_LIMIT)
                        continue;
                    int dist = std::abs(s.x - from_x) + std::abs(s.z - from_z);
                    if (dist <= limit) near.push_back({s.x, s.z});
                }
                if (!near.empty()) {
                    auto& pick = near[rng.get_int(0, (int) near.size() - 1)];
                    return {pick.x, pick.z};
                }
            }

            // All tiles crowded — pick the least-crowded closest to agent
            int fallback_x = spots[0].x, fallback_z = spots[0].z;
            int fallback_crowd = grid->at(spots[0].x, spots[0].z).agent_count;
            int fallback_dist =
                std::abs(spots[0].x - from_x) + std::abs(spots[0].z - from_z);
            for (size_t i = 1; i < spots.size(); i++) {
                int crowd = grid->at(spots[i].x, spots[i].z).agent_count;
                int dist = std::abs(spots[i].x - from_x) +
                           std::abs(spots[i].z - from_z);
                if (crowd < fallback_crowd ||
                    (crowd == fallback_crowd && dist < fallback_dist)) {
                    fallback_crowd = crowd;
                    fallback_dist = dist;
                    fallback_x = spots[i].x;
                    fallback_z = spots[i].z;
                }
            }
            return {fallback_x, fallback_z};
        }
    }

    // Fallback — no cached spots
    int gx = static_cast<int>(scx) + rng.get_int(-2, 2);
    int gz = static_cast<int>(scz) + rng.get_int(-2, 2);
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

            // Spawn agent heading toward the closest open stage spot
            auto [sx, sz] = best_stage_spot(SPAWN_X, SPAWN_Z);
            make_agent(SPAWN_X, SPAWN_Z, FacilityType::Stage, sx, sz);
        }
    }
};

// Check if a facility tile is "full" (too many agents)
static bool facility_is_full(int gx, int gz, const Grid& grid) {
    if (!grid.in_bounds(gx, gz)) return true;
    return grid.at(gx, gz).agent_count >= FACILITY_MAX_AGENTS;
}

// Find closest facility of a given type using cached positions.
// Returns the closest non-full tile, or closest full one for urgent needs.
static std::pair<int, int> find_nearest_facility(int from_x, int from_z,
                                                 TileType type, Grid& grid,
                                                 bool urgent = false) {
    const auto& positions = grid.get_facility_positions(type);
    if (positions.empty()) return {-1, -1};

    // Find closest non-full and closest overall in a single pass
    int best_x = -1, best_z = -1, best_dist = INT_MAX;
    int closest_x = -1, closest_z = -1, closest_dist = INT_MAX;

    for (auto [x, z] : positions) {
        int dist = std::abs(x - from_x) + std::abs(z - from_z);
        if (dist < closest_dist) {
            closest_dist = dist;
            closest_x = x;
            closest_z = z;
        }
        if (!facility_is_full(x, z, grid) && dist < best_dist) {
            best_dist = dist;
            best_x = x;
            best_z = z;
        }
    }

    if (best_x >= 0) return {best_x, best_z};
    if (urgent) return {closest_x, closest_z};
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

        float need_dt = event_flags::heat_active ? dt * 2.0f : dt;
        needs.bathroom_timer += need_dt;
        needs.food_timer += need_dt;

        if (!needs.needs_bathroom &&
            needs.bathroom_timer >= needs.bathroom_threshold) {
            needs.needs_bathroom = true;
        }
        if (!needs.needs_food && needs.food_timer >= needs.food_threshold) {
            needs.needs_food = true;
        }
    }
};

// Select agent's goal based on need priority: bathroom > food > stage.
// Skips agents that are already heading to the correct facility type
// to avoid redundant find_nearest_facility calls every frame.
struct UpdateAgentGoalSystem : System<Agent, AgentNeeds, Transform> {
    void for_each_with(Entity& e, Agent& agent, AgentNeeds& needs,
                       Transform& tf, float) override {
        if (skip_game_logic()) return;
        if (!e.is_missing<BeingServiced>()) return;
        if (!e.is_missing<WatchingStage>()) return;

        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;

        // Determine what the agent SHOULD want based on current needs
        FacilityType desired = FacilityType::Stage;
        bool urgent = false;

        bool needs_medical =
            (!e.is_missing<AgentHealth>() && e.get<AgentHealth>().hp < 0.4f);
        if (needs_medical) {
            desired = FacilityType::MedTent;
            urgent = true;
        } else if (needs.needs_bathroom) {
            desired = FacilityType::Bathroom;
            urgent = true;
        } else if (needs.needs_food) {
            desired = FacilityType::Food;
            urgent = false;
        }

        auto [cur_gx, cur_gz] =
            grid->world_to_grid(tf.position.x, tf.position.y);

        // Already heading to the right facility with a valid target?
        if (agent.want == desired && agent.target_grid_x >= 0) {
            // For Stage targets, re-evaluate if the target tile got crowded
            // (orange on the density overlay). This makes agents spread out
            // to emptier tiles instead of piling onto one spot.
            if (desired == FacilityType::Stage &&
                grid->in_bounds(agent.target_grid_x, agent.target_grid_z)) {
                int target_crowd =
                    grid->at(agent.target_grid_x, agent.target_grid_z)
                        .agent_count;
                if (target_crowd >= STAGE_CROWD_LIMIT) {
                    auto [rsx, rsz] = best_stage_spot(cur_gx, cur_gz);
                    agent.target_grid_x = rsx;
                    agent.target_grid_z = rsz;
                }
            }
            return;
        }

        if (desired == FacilityType::Stage) {
            if (agent.want != FacilityType::Stage &&
                agent.want != FacilityType::MedTent) {
                agent.want = FacilityType::Stage;
                auto [rsx, rsz] = best_stage_spot(cur_gx, cur_gz);
                agent.target_grid_x = rsx;
                agent.target_grid_z = rsz;
            }
        } else {
            TileType tile_type = facility_type_to_tile(desired);
            auto [fx, fz] =
                find_nearest_facility(cur_gx, cur_gz, tile_type, *grid, urgent);
            if (fx >= 0) {
                agent.want = desired;
                agent.target_grid_x = fx;
                agent.target_grid_z = fz;
            } else if (desired == FacilityType::Food) {
                // All food full — go back to stage
                needs.needs_food = false;
                needs.food_timer = 0.f;
                agent.want = FacilityType::Stage;
                auto [rsx, rsz] = best_stage_spot(cur_gx, cur_gz);
                agent.target_grid_x = rsx;
                agent.target_grid_z = rsz;
            }
        }
    }
};

// When agent reaches their assigned stage spot, start watching
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
            }
            return;
        }

        // Being serviced? Skip.
        if (!e.is_missing<BeingServiced>()) return;

        // Only start watching if heading to stage
        if (agent.want != FacilityType::Stage) return;

        // Must be standing on a StageFloor tile
        auto [gx, gz] = grid->world_to_grid(tf.position.x, tf.position.y);
        if (!grid->in_bounds(gx, gz) ||
            grid->at(gx, gz).type != TileType::StageFloor)
            return;

        // Only start watching once at or very near the target (within 2 tiles).
        // The tolerance handles cases where the exact target is unreachable
        // (e.g., blocked by Stage building or occupied).
        int tdx = std::abs(gx - agent.target_grid_x);
        int tdz = std::abs(gz - agent.target_grid_z);
        if (tdx + tdz > 2) return;

        auto& rng = RandomEngine::get();
        e.addComponent<WatchingStage>();
        e.get<WatchingStage>().watch_duration = rng.get_float(30.f, 120.f);
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
                } else if (bs.facility_type == FacilityType::MedTent) {
                    // Heal the agent
                    if (!e.is_missing<AgentHealth>()) {
                        e.get<AgentHealth>().hp = 1.0f;
                    }
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

                // Reset goal to stage — closest non-crowded spot
                agent.want = FacilityType::Stage;
                auto [fgx, fgz] =
                    grid->world_to_grid(tf.position.x, tf.position.y);
                auto [rsx, rsz] = best_stage_spot(fgx, fgz);
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
        bool at_target = (cur_type == facility_type_to_tile(agent.want));

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

// Map FacilityType to pheromone channel index.
// FacilityType enum order matches Tile::PHERO_* constants exactly.
static int facility_to_channel(FacilityType type) {
    return static_cast<int>(type);
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

        // Keep gate pheromone at max during Exodus (uses cached positions)
        grid->ensure_caches();
        for (auto [gx, gz] : grid->gate_positions) {
            grid->at(gx, gz).pheromone[Tile::PHERO_EXIT] = 255;
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

// Decay all pheromones periodically (time-based, not frame-rate dependent)
struct DecayPheromonesSystem : System<> {
    float accumulator = 0.f;
    static constexpr float DECAY_INTERVAL = 1.5f;  // seconds between decays

    void once(float dt) override {
        if (skip_game_logic()) return;
        accumulator += dt;
        if (accumulator < DECAY_INTERVAL) return;
        accumulator -= DECAY_INTERVAL;

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

// Apply crush damage to agents on critically dense tiles.
// Iterates agents once (O(n)) instead of per-tile agent queries (O(tiles*n)).
struct CrushDamageSystem : System<Agent, Transform, AgentHealth> {
    void for_each_with(Entity& e, Agent&, Transform& tf, AgentHealth& health,
                       float dt) override {
        if (skip_game_logic()) return;
        if (!e.is_missing<BeingServiced>()) return;

        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;

        auto [gx, gz] = grid->world_to_grid(tf.position.x, tf.position.y);
        if (!grid->in_bounds(gx, gz)) return;

        float density = grid->at(gx, gz).agent_count /
                        static_cast<float>(MAX_AGENTS_PER_TILE);
        if (density >= DENSITY_CRITICAL) {
            health.hp -= CRUSH_DAMAGE_RATE * dt;
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
                get_audio().play_death();
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
                // Clear manual overrides when closing debug panel
                if (!gs->show_debug) {
                    auto* ss = EntityHelper::get_singleton_cmp<SpawnState>();
                    if (ss) ss->manual_override = false;
                    auto* clk = EntityHelper::get_singleton_cmp<GameClock>();
                    if (clk) clk->debug_time_mult = 0.f;
                }
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
                spawn_toast("New facility slots unlocked!");
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
            get_audio().play_gameover();
            get_audio().stop_music();
            save::update_meta_on_game_over();
            save::delete_save();
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
            reset_game_state();
        }
    }
};

// Random events system: triggers events that affect gameplay
struct RandomEventSystem : System<> {
    void once(float dt) override {
        if (skip_game_logic()) return;
        auto* diff = EntityHelper::get_singleton_cmp<DifficultyState>();
        if (!diff) return;

        diff->event_timer += dt;

        // Check for active events
        auto active_events =
            EntityQuery().whereHasComponent<ActiveEvent>().gen();

        // Tick active events and check for completion
        bool any_active = false;
        for (Entity& ev_entity : active_events) {
            auto& ev = ev_entity.get<ActiveEvent>();
            ev.elapsed += dt;
            if (ev.elapsed >= ev.duration) {
                spawn_toast(ev.description + " has ended.");
                ev_entity.cleanup = true;
            } else {
                any_active = true;
            }
        }

        // Spawn new event?
        if (diff->event_timer < diff->next_event_time) return;

        // Don't stack events (check via flag instead of mid-frame
        // cleanup+query)
        if (any_active) return;

        diff->event_timer = 0.f;
        // Next event sooner as difficulty increases
        auto& rng = RandomEngine::get();
        diff->next_event_time =
            rng.get_float(90.f, 180.f) / diff->spawn_rate_mult;

        // Pick random event
        int event_id = rng.get_int(0, 3);
        Entity& ev_entity = EntityHelper::createEntity();
        ev_entity.addComponent<ActiveEvent>();
        auto& ev = ev_entity.get<ActiveEvent>();

        switch (event_id) {
            case 0:  // Rain
                ev.type = EventType::Rain;
                ev.duration = rng.get_float(30.f, 60.f);
                ev.description = "Rain storm";
                break;
            case 1:  // Power outage
                ev.type = EventType::PowerOutage;
                ev.duration = rng.get_float(15.f, 30.f);
                ev.description = "Power outage";
                break;
            case 2:  // VIP visit
                ev.type = EventType::VIPVisit;
                ev.duration = rng.get_float(30.f, 60.f);
                ev.description = "VIP visit";
                break;
            case 3:  // Heat wave
                ev.type = EventType::HeatWave;
                ev.duration = rng.get_float(20.f, 45.f);
                ev.description = "Heat wave";
                break;
        }

        // Notify player
        spawn_toast("Event: " + ev.description + "!");
    }
};

// Track active event flags for other systems to query.
// Sets flags each frame so movement/need systems can check them cheaply.
struct ApplyEventEffectsSystem : System<> {
    void once(float) override {
        event_flags::rain_active = false;
        event_flags::heat_active = false;

        if (skip_game_logic()) return;
        auto events = EntityQuery().whereHasComponent<ActiveEvent>().gen();

        for (Entity& ev_entity : events) {
            auto& ev = ev_entity.get<ActiveEvent>();
            switch (ev.type) {
                case EventType::Rain:
                    event_flags::rain_active = true;
                    break;
                case EventType::PowerOutage:
                    break;
                case EventType::VIPVisit:
                    break;
                case EventType::HeatWave:
                    event_flags::heat_active = true;
                    break;
            }
        }
    }
};

// Difficulty scaling: increase spawn rate and crowd sizes over time
struct DifficultyScalingSystem : System<> {
    int last_hour = -1;

    void once(float) override {
        if (skip_game_logic()) return;
        auto* diff = EntityHelper::get_singleton_cmp<DifficultyState>();
        auto* clock = EntityHelper::get_singleton_cmp<GameClock>();
        auto* spawn = EntityHelper::get_singleton_cmp<SpawnState>();
        if (!diff || !clock || !spawn) return;

        // Track day transitions
        int hour = clock->get_hour();
        if (last_hour >= 3 && last_hour < 10 && hour >= 10) {
            diff->day_number++;
            // Escalate difficulty each day
            diff->spawn_rate_mult = 1.0f + (diff->day_number - 1) * 0.15f;
            diff->crowd_size_mult = 1.0f + (diff->day_number - 1) * 0.1f;

            spawn_toast(fmt::format("Day {} begins!", diff->day_number));
        }
        last_hour = hour;

        // Apply spawn rate scaling (skip if debug override)
        if (!spawn->manual_override) {
            spawn->interval = DEFAULT_SPAWN_INTERVAL / diff->spawn_rate_mult;
        }
    }
};

// Quick save/load system: F5 to save, F9 to load
struct SaveLoadSystem : System<> {
    void once(float) override {
        if (game_is_over()) return;
        if (action_pressed(InputAction::QuickSave)) {
            if (save::save_game()) {
                spawn_toast("Game saved!", 2.0f);
                log_info("Game saved to {}", save::SAVE_FILE);
            }
        }
        if (action_pressed(InputAction::QuickLoad)) {
            if (save::load_game()) {
                spawn_toast("Game loaded!", 2.0f);
                log_info("Game loaded from {}", save::SAVE_FILE);
            }
        }
    }
};

// Update audio: drive beat music based on stage performance state
struct UpdateAudioSystem : System<> {
    void once(float) override {
        if (!get_audio().initialized) return;
        auto* sched = EntityHelper::get_singleton_cmp<ArtistSchedule>();
        bool performing = sched && sched->stage_state == StageState::Performing;
        get_audio().update(performing && !game_is_over());
    }
};

void register_update_systems(SystemManager& sm) {
    sm.register_update_system(std::make_unique<CameraInputSystem>());
    sm.register_update_system(std::make_unique<UpdateGameClockSystem>());
    sm.register_update_system(std::make_unique<ApplyEventEffectsSystem>());
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
    sm.register_update_system(std::make_unique<RandomEventSystem>());
    sm.register_update_system(std::make_unique<DifficultyScalingSystem>());
    sm.register_update_system(std::make_unique<SaveLoadSystem>());
    sm.register_update_system(std::make_unique<UpdateAudioSystem>());
}
