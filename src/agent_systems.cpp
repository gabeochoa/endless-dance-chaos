// Agent domain: movement, pathfinding, goals, stage watching, facility service.
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "afterhours/src/core/entity_query.h"
#include "components.h"
#include "engine/random_engine.h"
#include "systems.h"
#include "update_helpers.h"

// Greedy neighbor pathfinding with pheromone weighting.
// Pheromone reduces effective distance score, creating emergent trails.
// When stuck behind an obstacle, takes a random lateral step to route around.
static std::pair<int, int> pick_next_tile(
    int cur_x, int cur_z, int goal_x, int goal_z, const Grid& grid,
    FacilityType want = FacilityType::Stage) {
    static constexpr int dx[] = {1, -1, 0, 0};
    static constexpr int dz[] = {0, 0, 1, -1};

    // Shuffle direction order to eliminate directional bias.
    // Without this, agents always prefer right > left > down > up when
    // scores are tied, causing them to cluster on one side of the stage.
    auto& rng = RandomEngine::get();
    int order[4] = {0, 1, 2, 3};
    for (int k = 3; k > 0; k--) {
        int j = rng.get_int(0, k);
        std::swap(order[k], order[j]);
    }

    int best_x = cur_x;
    int best_z = cur_z;
    float cur_dist =
        static_cast<float>(std::abs(cur_x - goal_x) + std::abs(cur_z - goal_z));
    float best_score = cur_dist;
    bool best_is_path = false;

    int channel = facility_to_channel(want);

    struct Neighbor {
        int x, z;
        float score, dist;
        bool is_path;
    };
    std::array<Neighbor, 4> walkable;
    int walkable_count = 0;

    for (int idx = 0; idx < 4; idx++) {
        int i = order[idx];
        int nx = cur_x + dx[i];
        int nz = cur_z + dz[i];
        if (!grid.in_bounds(nx, nz)) continue;

        TileType type = grid.at(nx, nz).type;
        if (tile_blocks_movement(type)) continue;
        if (grid.at(nx, nz).agent_count >= MAX_AGENTS_PER_TILE) continue;

        float dist =
            static_cast<float>(std::abs(nx - goal_x) + std::abs(nz - goal_z));
        float phero =
            channel >= 0 ? Tile::to_strength(grid.at(nx, nz).pheromone[channel])
                         : 0.f;
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
    if (best_x == cur_x && best_z == cur_z && walkable_count > 0) {
        for (int k = walkable_count - 1; k > 0; k--) {
            int j = rng.get_int(0, k);
            std::swap(walkable[k], walkable[j]);
        }
        for (int i = 0; i < walkable_count; i++) {
            if (walkable[i].dist <= cur_dist + 2.f) {
                return {walkable[i].x, walkable[i].z};
            }
        }
        return {walkable[0].x, walkable[0].z};
    }

    return {best_x, best_z};
}

// Density-based speed modifier: slows agents in dangerous zones (75-90%)
static float density_speed_modifier(float density_ratio) {
    if (density_ratio < DENSITY_DANGEROUS) return 1.0f;
    if (density_ratio >= DENSITY_CRITICAL) return 0.1f;
    float t = (density_ratio - DENSITY_DANGEROUS) /
              (DENSITY_CRITICAL - DENSITY_DANGEROUS);
    return 1.0f - (t * 0.9f);
}

// When density is dangerous, randomly pick a less-crowded walkable neighbor.
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

    if (n == 0) {
        float density = cur_count / static_cast<float>(MAX_AGENTS_PER_TILE);
        if (density >= DENSITY_CRITICAL) {
            for (auto [dx, dz] : dirs) {
                int nx = cx + dx;
                int nz = cz + dz;
                if (!grid.in_bounds(nx, nz)) continue;
                if (tile_blocks_movement(grid.at(nx, nz).type)) continue;
                candidates[n++] = {nx, nz, grid.at(nx, nz).agent_count};
            }
            if (n > 0) {
                int best = 0;
                for (int i = 1; i < n; i++) {
                    if (candidates[i].count < candidates[best].count) best = i;
                }
                static int desperate_count = 0;
                if (++desperate_count <= 5) {
                    log_warn(
                        "DESPERATE FLEE ({},{}) count={} -> ({},{}) count={}",
                        cx, cz, cur_count, candidates[best].x,
                        candidates[best].z, candidates[best].count);
                }
                return {candidates[best].x, candidates[best].z};
            }
            static int flee_trapped_count = 0;
            if (++flee_trapped_count <= 5) {
                log_warn("FLEE TRAPPED at ({},{}) count={} type={}:", cx, cz,
                         cur_count, static_cast<int>(grid.at(cx, cz).type));
                for (auto [ddx, ddz] : dirs) {
                    int nx2 = cx + ddx;
                    int nz2 = cz + ddz;
                    if (!grid.in_bounds(nx2, nz2)) {
                        log_warn("  ({},{}) OOB", nx2, nz2);
                    } else {
                        auto& t2 = grid.at(nx2, nz2);
                        log_warn("  ({},{}) type={} blocks={} count={}", nx2,
                                 nz2, static_cast<int>(t2.type),
                                 tile_blocks_movement(t2.type), t2.agent_count);
                    }
                }
            }
        }
        return {cx, cz};
    }
    if (n == 1) return {candidates[0].x, candidates[0].z};

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

// Distance from tile (x,z) to the nearest edge of the stage building.
static float dist_to_stage_edge(int x, int z) {
    float dx =
        std::max(0.f, std::max((float) (STAGE_X - x),
                               (float) (x - (STAGE_X + STAGE_SIZE - 1))));
    float dz =
        std::max(0.f, std::max((float) (STAGE_Z - z),
                               (float) (z - (STAGE_Z + STAGE_SIZE - 1))));
    return std::sqrt(dx * dx + dz * dz);
}

// Pick the best StageFloor tile scored by distance to stage edge + crowd.
std::pair<int, int> best_stage_spot(int /*from_x*/, int /*from_z*/) {
    auto* grid = EntityHelper::get_singleton_cmp<Grid>();
    auto& rng = RandomEngine::get();
    float scx = STAGE_X + STAGE_SIZE / 2.0f;
    float scz = STAGE_Z + STAGE_SIZE / 2.0f;

    if (grid) {
        grid->ensure_caches();
        const auto& spots = grid->stage_floor_spots;

        if (!spots.empty()) {
            constexpr float CROWD_PENALTY = 2.0f;
            constexpr float SCORE_BAND = 2.0f;

            float best_score = 1e9f;
            for (const auto& s : spots) {
                float score = dist_to_stage_edge(s.x, s.z) +
                              grid->at(s.x, s.z).agent_count * CROWD_PENALTY;
                if (score < best_score) best_score = score;
            }

            struct Candidate {
                int x, z;
            };
            std::vector<Candidate> band;
            band.reserve(24);
            float limit = best_score + SCORE_BAND;
            for (const auto& s : spots) {
                float score = dist_to_stage_edge(s.x, s.z) +
                              grid->at(s.x, s.z).agent_count * CROWD_PENALTY;
                if (score <= limit) band.push_back({s.x, s.z});
            }

            if (!band.empty()) {
                auto& pick = band[rng.get_int(0, (int) band.size() - 1)];
                return {pick.x, pick.z};
            }
        }
    }

    int gx = static_cast<int>(scx) + rng.get_int(-2, 2);
    int gz = static_cast<int>(scz) + rng.get_int(-2, 2);
    gx = std::clamp(gx, PLAY_MIN, PLAY_MAX);
    gz = std::clamp(gz, PLAY_MIN, PLAY_MAX);
    return {gx, gz};
}

// Move agents toward their target using greedy pathfinding
struct AgentMovementSystem : System<Agent, Transform> {
    void for_each_with(Entity& e, Agent& agent, Transform& tf,
                       float dt) override {
        if (skip_game_logic()) return;
        if (!e.is_missing<BeingServiced>()) return;
        if (!e.is_missing<Collapsed>()) return;  // collapsed: medics or death

        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();

        auto [cur_gx, cur_gz] =
            grid->world_to_grid(tf.position.x, tf.position.y);

        // Stuck detection
        if (cur_gx != agent.last_grid_x || cur_gz != agent.last_grid_z) {
            agent.stuck_timer = 0.f;
            agent.last_grid_x = cur_gx;
            agent.last_grid_z = cur_gz;
        } else {
            agent.stuck_timer += dt;
        }

        bool forcing = agent.is_forcing();

        // Check if this tile is dangerously crowded
        bool fleeing = false;
        int next_x = cur_gx, next_z = cur_gz;
        if (grid->in_bounds(cur_gx, cur_gz)) {
            float density = grid->at(cur_gx, cur_gz).agent_count /
                            static_cast<float>(MAX_AGENTS_PER_TILE);
            bool lethal = density >= DENSITY_CRITICAL;
            bool dangerous = density >= DENSITY_DANGEROUS;

            if (lethal || (dangerous && !forcing)) {
                bool need_new_target = true;

                if (agent.flee_target_x >= 0 &&
                    (cur_gx != agent.flee_target_x ||
                     cur_gz != agent.flee_target_z)) {
                    bool target_ok = true;
                    if (grid->in_bounds(agent.flee_target_x,
                                        agent.flee_target_z)) {
                        float td =
                            grid->at(agent.flee_target_x, agent.flee_target_z)
                                .agent_count /
                            static_cast<float>(MAX_AGENTS_PER_TILE);
                        if (td >= DENSITY_CRITICAL) target_ok = false;
                    }
                    if (target_ok) {
                        next_x = agent.flee_target_x;
                        next_z = agent.flee_target_z;
                        fleeing = true;
                        need_new_target = false;
                    }
                }

                if (need_new_target) {
                    auto [fx, fz] = pick_flee_tile(cur_gx, cur_gz, *grid);
                    if (fx != cur_gx || fz != cur_gz) {
                        next_x = fx;
                        next_z = fz;
                        agent.flee_target_x = fx;
                        agent.flee_target_z = fz;
                        fleeing = true;
                    } else if (lethal) {
                        if (!e.is_missing<WatchingStage>()) {
                            e.removeComponent<WatchingStage>();
                        }
                        auto [rsx, rsz] = best_stage_spot(cur_gx, cur_gz);
                        agent.set_target(rsx, rsz);
                        static int lethal_count = 0;
                        if (++lethal_count <= 5) {
                            log_warn(
                                "LETHAL NO FLEE at ({},{}) count={} "
                                "forcing={} stuck={:.1f}s -> retarget ({},{})",
                                cur_gx, cur_gz,
                                grid->at(cur_gx, cur_gz).agent_count, forcing,
                                agent.stuck_timer, rsx, rsz);
                        }
                    }
                }
                if (fleeing) {
                    agent.move_target_x = -1;
                    agent.move_target_z = -1;
                    if (!e.is_missing<WatchingStage>()) {
                        e.removeComponent<WatchingStage>();
                    }
                    agent.speed = SPEED_PATH;
                    if (gs) agent.speed *= gs->speed_multiplier;
                    if (event_flags::rain_active) agent.speed *= 0.5f;
                    // Security Post: faster density relief in its radius.
                    if (near_tile_type(*grid, cur_gx, cur_gz,
                                       TileType::SecurityPost, SECURITY_RADIUS))
                        agent.speed *= SECURITY_FLEE_MULT;
                }
            } else if (!dangerous) {
                agent.flee_target_x = -1;
                agent.flee_target_z = -1;
            }
        }

        if (!fleeing) {
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
                 cur_type == TileType::StageFloor ||
                 cur_type == TileType::Bathroom || cur_type == TileType::Food ||
                 cur_type == TileType::MedTent || cur_type == TileType::Bar ||
                 cur_type == TileType::SecurityPost ||
                 cur_type == TileType::WaterStation)
                    ? SPEED_PATH
                    : SPEED_GRASS;

            if (!forcing && grid->in_bounds(cur_gx, cur_gz)) {
                float density = grid->at(cur_gx, cur_gz).agent_count /
                                static_cast<float>(MAX_AGENTS_PER_TILE);
                if (density < DENSITY_CRITICAL) {
                    agent.speed *= density_speed_modifier(density);
                }
            }
            if (gs) agent.speed *= gs->speed_multiplier;
            if (event_flags::rain_active) agent.speed *= 0.5f;

            bool need_pathfind =
                agent.move_target_x < 0 || (cur_gx == agent.move_target_x &&
                                            cur_gz == agent.move_target_z);
            if (need_pathfind) {
                auto [px, pz] =
                    pick_next_tile(cur_gx, cur_gz, agent.target_grid_x,
                                   agent.target_grid_z, *grid, agent.want);
                agent.move_target_x = px;
                agent.move_target_z = pz;
            }

            if (forcing &&
                grid->in_bounds(agent.move_target_x, agent.move_target_z)) {
                float next_density =
                    grid->at(agent.move_target_x, agent.move_target_z)
                        .agent_count /
                    static_cast<float>(MAX_AGENTS_PER_TILE);
                if (next_density >= DENSITY_CRITICAL) {
                    return;
                }
            }

            next_x = agent.move_target_x;
            next_z = agent.move_target_z;
        }

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

// Check if a facility tile is "full"
static bool facility_is_full(int gx, int gz, const Grid& grid) {
    if (!grid.in_bounds(gx, gz)) return true;
    return grid.at(gx, gz).agent_count >= FACILITY_MAX_AGENTS;
}

// Find closest facility of a given type using cached positions.
static std::pair<int, int> find_nearest_facility(int from_x, int from_z,
                                                 TileType type, Grid& grid,
                                                 bool urgent = false) {
    const auto& positions = grid.get_facility_positions(type);
    if (positions.empty()) return {-1, -1};

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

// Tick need timers and set need flags.
// Heat grows over time, faster in dense crowds and during heat waves, and
// slower near a Water Station. At max the agent collapses (an injury).
// Bar thirst only builds when at least one Bar exists on the map.
struct NeedTickSystem : System<Agent, AgentNeeds, Transform> {
    float collapse_toast_cooldown = 0.f;

    // Once per frame (not per agent) so the toast throttle stays real-time.
    void once(float dt) override { collapse_toast_cooldown -= dt; }

    void for_each_with(Entity& e, Agent&, AgentNeeds& needs, Transform& tf,
                       float dt) override {
        if (skip_game_logic()) return;
        if (!e.is_missing<BeingServiced>()) return;
        if (!e.is_missing<WatchingStage>()) return;
        if (!e.is_missing<Collapsed>()) return;

        float need_dt = event_flags::heat_active ? dt * 2.0f : dt;
        needs.bathroom_timer += need_dt;
        needs.food_timer += need_dt;
        needs.bar_timer += need_dt;

        if (!needs.needs_bathroom &&
            needs.bathroom_timer >= needs.bathroom_threshold) {
            needs.needs_bathroom = true;
        }
        if (!needs.needs_food && needs.food_timer >= needs.food_threshold) {
            needs.needs_food = true;
        }

        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (grid) {
            auto [gx, gz] = grid->world_to_grid(tf.position.x, tf.position.y);
            bool has_bar = !grid->get_facility_positions(TileType::Bar).empty();
            if (!needs.needs_bar && has_bar &&
                needs.bar_timer >= needs.bar_threshold) {
                needs.needs_bar = true;
            }

            // Heat: base rate + crowd-density bonus, halved near water.
            float heat_dt = need_dt;
            if (grid->in_bounds(gx, gz)) {
                float density = grid->at(gx, gz).agent_count /
                                static_cast<float>(MAX_AGENTS_PER_TILE);
                heat_dt *= 1.0f + density * 2.0f;
            }
            if (near_tile_type(*grid, gx, gz, TileType::WaterStation,
                               WATER_RADIUS))
                heat_dt *= 0.5f;
            needs.heat_timer += heat_dt;
        } else {
            needs.heat_timer += need_dt;
        }

        // At 75% the agent seeks relief (Water Station, else MedTent);
        // at 100% they collapse.
        if (!needs.needs_heat &&
            needs.heat_timer >= needs.heat_threshold * 0.75f) {
            needs.needs_heat = true;
        }
        if (needs.heat_timer >= needs.heat_threshold) {
            needs.heat_timer = needs.heat_threshold;  // clamp: one collapse
            if (e.is_missing<Collapsed>()) {
                e.addComponent<Collapsed>();
                auto* gs = EntityHelper::get_singleton_cmp<GameState>();
                if (gs) gs->injury_count++;
                if (collapse_toast_cooldown <= 0.f) {
                    collapse_toast_cooldown = 5.0f;
                    spawn_toast(
                        "An attendee collapsed from the heat! Medics, go!",
                        5.0f);
                }
            }
        }
    }
};

// Select agent's goal based on need priority:
// medical > bathroom > food > heat > bar > stage.
struct UpdateAgentGoalSystem : System<Agent, AgentNeeds, Transform> {
    void for_each_with(Entity& e, Agent& agent, AgentNeeds& needs,
                       Transform& tf, float) override {
        if (skip_game_logic()) return;
        if (!e.is_missing<BeingServiced>()) return;
        if (!e.is_missing<WatchingStage>()) return;
        if (!e.is_missing<Collapsed>()) return;

        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;

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
        } else if (needs.needs_heat) {
            // Water Station relieves heat; fall back to the MedTent when the
            // map has no water.
            if (!grid->get_facility_positions(TileType::WaterStation).empty())
                desired = FacilityType::WaterStation;
            else
                desired = FacilityType::MedTent;
            urgent = false;
        } else if (needs.needs_bar) {
            desired = FacilityType::Bar;
            urgent = false;
        }

        auto [cur_gx, cur_gz] =
            grid->world_to_grid(tf.position.x, tf.position.y);

        if (agent.want == desired && agent.target_grid_x >= 0) {
            if (desired == FacilityType::Stage &&
                grid->in_bounds(agent.target_grid_x, agent.target_grid_z)) {
                constexpr int RETARGET_THRESHOLD = 3;
                int target_crowd =
                    grid->at(agent.target_grid_x, agent.target_grid_z)
                        .agent_count;
                if (target_crowd >= RETARGET_THRESHOLD) {
                    auto [rsx, rsz] = best_stage_spot(cur_gx, cur_gz);
                    agent.set_target(rsx, rsz);
                }
            }
            return;
        }

        if (desired == FacilityType::Stage) {
            if (agent.want != FacilityType::Stage &&
                agent.want != FacilityType::MedTent) {
                agent.want = FacilityType::Stage;
                auto [rsx, rsz] = best_stage_spot(cur_gx, cur_gz);
                agent.set_target(rsx, rsz);
            }
        } else {
            TileType tile_type = facility_type_to_tile(desired);
            auto [fx, fz] =
                find_nearest_facility(cur_gx, cur_gz, tile_type, *grid, urgent);
            if (fx >= 0) {
                agent.want = desired;
                agent.set_target(fx, fz);
            } else if (desired == FacilityType::Food) {
                needs.needs_food = false;
                needs.food_timer = 0.f;
                agent.want = FacilityType::Stage;
                auto [rsx, rsz] = best_stage_spot(cur_gx, cur_gz);
                agent.set_target(rsx, rsz);
            } else if (desired == FacilityType::Bar) {
                // Bar demolished mid-trip: drop the craving, head to stage.
                needs.needs_bar = false;
                needs.bar_timer = 0.f;
                needs.bar_threshold = 120.f;
                agent.want = FacilityType::Stage;
                auto [rsx, rsz] = best_stage_spot(cur_gx, cur_gz);
                agent.set_target(rsx, rsz);
            } else if (desired == FacilityType::WaterStation) {
                // Water gone mid-trip: keep the heat need; next frame the
                // selector falls back to the MedTent. Just stop walking
                // toward the stale target.
                agent.want = FacilityType::Stage;
                auto [rsx, rsz] = best_stage_spot(cur_gx, cur_gz);
                agent.set_target(rsx, rsz);
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

        if (!e.is_missing<WatchingStage>()) {
            auto& ws = e.get<WatchingStage>();
            ws.watch_timer += dt;
            if (ws.watch_timer >= ws.watch_duration) {
                e.removeComponent<WatchingStage>();
            }
            return;
        }

        if (!e.is_missing<BeingServiced>()) return;
        if (agent.want != FacilityType::Stage) return;

        auto [gx, gz] = grid->world_to_grid(tf.position.x, tf.position.y);
        if (!grid->in_bounds(gx, gz) ||
            grid->at(gx, gz).type != TileType::StageFloor)
            return;

        if (gx != agent.target_grid_x || gz != agent.target_grid_z) return;

        auto& rng = RandomEngine::get();
        e.addComponent<WatchingStage>();
        float duration = rng.get_float(30.f, 120.f);
        // Bar service raises fun: the next set lasts half again as long.
        if (!e.is_missing<AgentNeeds>() &&
            e.get<AgentNeeds>().fun_boost > 0.f) {
            duration *= 1.5f;
            e.get<AgentNeeds>().fun_boost = 0.f;
        }
        e.get<WatchingStage>().watch_duration = duration;
    }
};

// Handle agents arriving at facilities: absorb, service, release
struct FacilityServiceSystem : System<Agent, AgentNeeds, Transform> {
    void for_each_with(Entity& e, Agent& agent, AgentNeeds& needs,
                       Transform& tf, float dt) override {
        if (skip_game_logic()) return;
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;

        if (!e.is_missing<BeingServiced>()) {
            auto& bs = e.get<BeingServiced>();
            bs.time_remaining -= dt;
            if (bs.time_remaining <= 0.f) {
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
                    if (!e.is_missing<AgentHealth>()) {
                        e.get<AgentHealth>().hp = 1.0f;
                    }
                    // MedTent also relieves heat/illness.
                    needs.needs_heat = false;
                    needs.heat_timer = 0.f;
                    needs.heat_threshold = rng.get_float(90.f, 180.f);
                } else if (bs.facility_type == FacilityType::Bar) {
                    // Bar: cash per served agent + a fun boost.
                    if (auto* econ =
                            EntityHelper::get_singleton_cmp<Economy>()) {
                        econ->earn(BAR_DRINK_PRICE);
                    }
                    needs.needs_bar = false;
                    needs.bar_timer = 0.f;
                    needs.bar_threshold = rng.get_float(60.f, 180.f);
                    needs.fun_boost = 1.0f;
                } else if (bs.facility_type == FacilityType::WaterStation) {
                    // Water Station: cheap heat/illness relief.
                    needs.needs_heat = false;
                    needs.heat_timer = 0.f;
                    needs.heat_threshold = rng.get_float(90.f, 180.f);
                }

                tf.position.x = bs.facility_grid_x * TILESIZE - TILESIZE;
                tf.position.y = bs.facility_grid_z * TILESIZE;

                if (e.is_missing<PheromoneDepositor>()) {
                    e.addComponent<PheromoneDepositor>();
                }
                auto& pdep = e.get<PheromoneDepositor>();
                pdep.leaving_type = bs.facility_type;
                pdep.is_depositing = true;
                pdep.deposit_distance = 0.f;

                e.removeComponent<BeingServiced>();

                agent.want = FacilityType::Stage;
                auto [fgx, fgz] =
                    grid->world_to_grid(tf.position.x, tf.position.y);
                auto [rsx, rsz] = best_stage_spot(fgx, fgz);
                agent.set_target(rsx, rsz);
            }
            return;
        }

        if (agent.want == FacilityType::Stage) return;
        // Collapsed agents never walk into a facility; rescue handles them.
        if (!e.is_missing<Collapsed>()) return;

        auto [gx, gz] = grid->world_to_grid(tf.position.x, tf.position.y);
        if (!grid->in_bounds(gx, gz)) return;

        TileType cur_type = grid->at(gx, gz).type;
        bool at_target = (cur_type == facility_type_to_tile(agent.want));

        if (at_target) {
            if (facility_is_full(gx, gz, *grid)) {
                return;
            }
            e.addComponent<BeingServiced>();
            auto& bs = e.get<BeingServiced>();
            bs.facility_grid_x = gx;
            bs.facility_grid_z = gz;
            bs.facility_type = agent.want;
            // Janitors cut service times park-wide (10% each, min 50%).
            bs.time_remaining = SERVICE_TIME * staff_service_time_factor();
        }
    }
};

void register_agent_goal_systems(SystemManager& sm) {
    sm.register_update_system(std::make_unique<NeedTickSystem>());
    sm.register_update_system(std::make_unique<UpdateAgentGoalSystem>());
}

void register_agent_movement_systems(SystemManager& sm) {
    sm.register_update_system(std::make_unique<AgentMovementSystem>());
    sm.register_update_system(std::make_unique<StageWatchingSystem>());
    sm.register_update_system(std::make_unique<FacilityServiceSystem>());
}
