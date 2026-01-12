// AFTER_HOURS_REPLACE_LOGGING and log.h must come first
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_query.h"
#include "components.h"
#include "entity_makers.h"
#include "game.h"
#include "rl.h"
#include "systems.h"
#include "vec_util.h"

struct CameraInputSystem : System<ProvidesCamera> {
    void for_each_with(Entity&, ProvidesCamera& cam, float dt) override {
        cam.cam.handle_input(dt);
    }
};

struct MousePickingSystem : System<ProvidesCamera, BuilderState> {
    void for_each_with(Entity&, ProvidesCamera& cam, BuilderState& builder,
                       float) override {
        if (!builder.active) return;

        // Get mouse ray from camera
        raylib::Ray ray =
            raylib::GetMouseRay(raylib::GetMousePosition(), cam.cam.camera);

        // Intersect with Y=0 ground plane
        // For orthographic camera, project along ray direction
        if (std::abs(ray.direction.y) < 0.0001f) {
            builder.hover_valid = false;
            return;
        }

        float t = -ray.position.y / ray.direction.y;
        if (t < 0) {
            builder.hover_valid = false;
            return;
        }

        vec3 hit = {ray.position.x + ray.direction.x * t, 0.f,
                    ray.position.z + ray.direction.z * t};

        // Snap to grid
        builder.hover_grid_x = (int) std::floor(hit.x / TILESIZE + 0.5f);
        builder.hover_grid_z = (int) std::floor(hit.z / TILESIZE + 0.5f);
        builder.hover_valid = true;

        // Check if path already exists at hover position
        builder.path_exists_at_hover =
            find_path_tile_at(builder.hover_grid_x, builder.hover_grid_z);
    }
};

struct PathStagingSystem : System<BuilderState, GameState> {
    void for_each_with(Entity&, BuilderState& builder, GameState& state,
                       float) override {
        if (!builder.active || !builder.hover_valid) return;
        if (state.is_game_over()) return;

        bool left_down = raylib::IsMouseButtonDown(raylib::MOUSE_LEFT_BUTTON);
        bool right_down = raylib::IsMouseButtonDown(raylib::MOUSE_RIGHT_BUTTON);
        bool shift_down = raylib::IsKeyDown(raylib::KEY_LEFT_SHIFT);

        bool is_removing = right_down || (left_down && shift_down);
        bool is_placing = left_down && !shift_down;

        int gx = builder.hover_grid_x;
        int gz = builder.hover_grid_z;

        // Don't add duplicates to pending queue
        if (builder.is_pending_at(gx, gz)) return;

        // Drag-to-place: as mouse moves over new tiles while held, add them
        if (is_placing && !find_path_tile_at(gx, gz)) {
            builder.pending_tiles.push_back({gx, gz, false});
        }

        // Drag-to-remove: queue existing tiles for removal
        if (is_removing && find_path_tile_at(gx, gz)) {
            builder.pending_tiles.push_back({gx, gz, true});
        }
    }
};

// Path confirmation - Enter commits, Escape cancels
struct PathConfirmationSystem : System<BuilderState> {
    void for_each_with(Entity&, BuilderState& builder, float) override {
        if (!builder.has_pending()) return;

        // Confirm with Enter key
        if (raylib::IsKeyPressed(raylib::KEY_ENTER)) {
            commit_pending_tiles(builder);
            builder.clear_pending();
        }

        // Cancel with Escape key
        if (raylib::IsKeyPressed(raylib::KEY_ESCAPE)) {
            builder.clear_pending();
        }
    }

    void commit_pending_tiles(BuilderState& builder) {
        // TODO: Check cost, deduct money here

        for (const auto& pending : builder.pending_tiles) {
            if (pending.is_removal) {
                remove_path_tile_at(pending.grid_x, pending.grid_z);
            } else {
                make_path_tile(pending.grid_x, pending.grid_z);
            }
        }

        // Only recalculate signposts once after all changes
        EntityHelper::merge_entity_arrays();
        EntityHelper::cleanup();
        calculate_path_signposts();

        log_info("Committed {} path tile changes",
                 builder.pending_tiles.size());
    }
};

// Tracks global stress levels and checks for lose condition
struct GameStateSystem : System<> {
    void once(float dt) override {
        auto* state = EntityHelper::get_singleton_cmp<GameState>();
        if (!state) return;

        // Toggle data layer overlay with TAB (like Cities: Skylines info views)
        if (raylib::IsKeyPressed(raylib::KEY_TAB)) {
            state->show_data_layer = !state->show_data_layer;
        }

        // Don't update if game is over
        if (state->is_game_over()) {
            state->game_over_timer += dt;
            return;
        }

        state->game_time += dt;

        // Calculate global stress metrics
        auto agents = EntityQuery()
                          .whereHasComponent<HasStress>()
                          .whereHasComponent<Agent>()
                          .gen();

        if (agents.empty()) {
            state->global_stress = 0.f;
            state->max_stress = 0.f;
            return;
        }

        float total_stress = 0.f;
        float max_stress = 0.f;
        int critical_count = 0;

        for (const Entity& agent : agents) {
            float stress = agent.get<HasStress>().stress;
            total_stress += stress;
            max_stress = std::max(max_stress, stress);
            if (stress >= 0.95f) critical_count++;
        }

        state->global_stress = total_stress / (float) agents.size();
        state->max_stress = max_stress;

        // TODO: Re-enable lose conditions after balancing gameplay
        // Check lose conditions
        // bool global_critical =
        //     state->global_stress >= GameState::CRITICAL_GLOBAL_STRESS;
        // bool too_many_critical =
        //     critical_count >= GameState::CRITICAL_MAX_STRESS_COUNT;
        //
        // if (global_critical || too_many_critical) {
        //     state->status = GameStatus::GameOver;
        //     state->game_over_timer = 0.f;
        // }
        (void) critical_count;  // Suppress unused warning
    }
};

// Find the nearest Facility matching the agent's want
struct TargetFindingSystem
    : System<Transform, Agent, AgentTarget, Not<InsideFacility>> {
    void for_each_with(Entity& e, Transform& t, Agent& a, AgentTarget& target,
                       float) override {
        // Skip if we already have a valid target
        if (target.facility_id >= 0) {
            auto existing = EntityHelper::getEntityForID(target.facility_id);
            if (existing.valid() && existing->has<Facility>()) {
                const Facility& f = existing->get<Facility>();
                // Check if facility still matches our want
                if (f.type == a.want) {
                    // Special check for stages - is it still open?
                    if (f.type == FacilityType::Stage &&
                        existing->has<StageInfo>()) {
                        const StageInfo& info = existing->get<StageInfo>();
                        if (info.state != StageState::Announcing &&
                            info.state != StageState::Performing) {
                            // Stage closed, need new target
                            target.facility_id = -1;
                        } else {
                            return;  // Stage still valid
                        }
                    } else {
                        return;  // Target is still valid
                    }
                }
            }
        }

        // Find nearest facility matching our want
        auto facilities =
            EntityQuery()
                .whereHasComponent<Transform>()
                .whereHasComponent<Facility>()
                .whereLambda([&a](const Entity& facility) {
                    const Facility& f = facility.get<Facility>();
                    if (f.type != a.want) return false;
                    // For stages, only target if announcing or performing
                    if (f.type == FacilityType::Stage &&
                        facility.has<StageInfo>()) {
                        const StageInfo& info = facility.get<StageInfo>();
                        if (info.state != StageState::Announcing &&
                            info.state != StageState::Performing) {
                            return false;  // Stage not accepting people
                        }
                    }
                    return true;
                })
                .orderByLambda([&t](const Entity& a, const Entity& b) {
                    float dist_a =
                        vec::distance(t.position, a.get<Transform>().position);
                    float dist_b =
                        vec::distance(t.position, b.get<Transform>().position);
                    return dist_a < dist_b;
                })
                .gen();

        int best_id = -1;
        vec2 best_pos{0, 0};
        if (!facilities.empty()) {
            const Entity& nearest = facilities.front();
            best_id = nearest.id;
            best_pos = nearest.get<Transform>().position;
        }

        // If wanting stage but no stage is open, pick a different activity
        if (a.want == FacilityType::Stage && best_id < 0) {
            a.want =
                (rand() % 2 == 0) ? FacilityType::Food : FacilityType::Bathroom;
            return;  // Will find new target next frame
        }

        target.facility_id = best_id;
        target.target_pos = best_pos;
    }
};

struct PathFollowingSystem
    : System<Transform, Agent, AgentTarget, AgentSteering> {
    // Pick a new random neighbor tile to wander to
    int pick_random_neighbor_tile(int current_tile_id) {
        auto current = EntityHelper::getEntityForID(current_tile_id);
        if (!current.valid() || !current->has<PathTile>()) return -1;

        const PathTile& pt = current->get<PathTile>();
        const int dx[] = {1, -1, 0, 0};
        const int dz[] = {0, 0, 1, -1};

        std::vector<int> neighbor_ids;

        auto path_tiles = EntityQuery().whereHasComponent<PathTile>().gen();
        for (Entity& tile : path_tiles) {
            const PathTile& tile_pt = tile.get<PathTile>();
            for (int i = 0; i < 4; i++) {
                if (tile_pt.grid_x == pt.grid_x + dx[i] &&
                    tile_pt.grid_z == pt.grid_z + dz[i]) {
                    neighbor_ids.push_back(tile.id);
                    break;
                }
            }
        }

        if (neighbor_ids.empty()) return -1;
        return neighbor_ids[rand() % neighbor_ids.size()];
    }

    vec2 get_wander_direction(const Transform& t, AgentSteering& steering,
                              int current_tile_id) {
        // Check if we need a new wander target
        bool need_new_target = (steering.wander_target_tile < 0);

        // Check if current wander target is still valid
        if (!need_new_target) {
            auto target_tile =
                EntityHelper::getEntityForID(steering.wander_target_tile);
            if (!target_tile.valid() || !target_tile->has<Transform>()) {
                need_new_target = true;
            } else {
                // Check if we've reached the target tile
                vec2 target_pos = target_tile->get<Transform>().position;
                float dist = vec::distance(t.position, target_pos);
                if (dist < TILESIZE * 0.5f) {
                    // Reached the target, pick a new one from this tile
                    need_new_target = true;
                    current_tile_id = steering.wander_target_tile;
                }
            }
        }

        if (need_new_target) {
            steering.wander_target_tile =
                pick_random_neighbor_tile(current_tile_id);
        }

        // If we still don't have a target, we're stuck
        if (steering.wander_target_tile < 0) return {0, 0};

        auto target_tile =
            EntityHelper::getEntityForID(steering.wander_target_tile);
        if (!target_tile.valid() || !target_tile->has<Transform>()) {
            steering.wander_target_tile = -1;
            return {0, 0};
        }

        vec2 target_pos = target_tile->get<Transform>().position;
        vec2 dir = {target_pos.x - t.position.x, target_pos.y - t.position.y};
        return vec::length(dir) > EPSILON ? vec::norm(dir) : vec2{0, 0};
    }

    void for_each_with(Entity&, Transform& t, Agent& a,
                       AgentTarget& agent_target, AgentSteering& steering,
                       float) override {
        auto score_tile = [&](const Entity& tile) {
            const Transform& tile_t = tile.get<Transform>();
            float dist = vec::distance(t.position, tile_t.position);
            const PathSignpost& sp = tile.get<PathSignpost>();
            int next_id = sp.get_next_node(a.want);
            float score = dist;
            if (next_id >= 0 && agent_target.facility_id >= 0) {
                auto next_tile = EntityHelper::getEntityForID(next_id);
                if (next_tile.valid() && next_tile->has<Transform>()) {
                    float next_to_target =
                        vec::distance(next_tile->get<Transform>().position,
                                      agent_target.target_pos);
                    score = dist + next_to_target * 0.1f;
                }
            }
            return score;
        };

        auto path_tiles =
            EntityQuery()
                .whereHasComponent<Transform>()
                .whereHasComponent<PathTile>()
                .whereHasComponent<PathSignpost>()
                .orderByLambda([&](const Entity& a, const Entity& b) {
                    return score_tile(a) < score_tile(b);
                })
                .gen();

        if (path_tiles.empty()) {
            steering.path_direction = {0, 0};
            return;
        }

        int closest_tile_id = path_tiles.front().get().id;

        auto closest = EntityHelper::getEntityForID(closest_tile_id);
        if (!closest.valid()) {
            steering.path_direction = {0, 0};
            return;
        }

        const PathSignpost& signpost = closest->get<PathSignpost>();
        int next_tile_id = signpost.get_next_node(a.want);

        if (next_tile_id < 0) {
            float dist_to_target =
                vec::distance(t.position, agent_target.target_pos);
            if (agent_target.facility_id >= 0 &&
                dist_to_target < TILESIZE * 1.5f) {
                vec2 dir = {agent_target.target_pos.x - t.position.x,
                            agent_target.target_pos.y - t.position.y};
                if (vec::length(dir) > EPSILON) {
                    steering.path_direction = vec::norm(dir);
                    return;
                }
            }
            steering.path_direction =
                get_wander_direction(t, steering, closest_tile_id);
            return;
        }

        // Go toward the next tile, but stay close to the path
        auto next_tile = EntityHelper::getEntityForID(next_tile_id);
        auto current_tile = EntityHelper::getEntityForID(closest_tile_id);
        if (!next_tile.valid() || !next_tile->has<Transform>() ||
            !current_tile.valid()) {
            // Next tile was deleted - fall back to wandering
            steering.path_direction =
                get_wander_direction(t, steering, closest_tile_id);
            return;
        }

        // Clear wander target when following a valid path
        steering.wander_target_tile = -1;

        vec2 current_pos = current_tile->get<Transform>().position;
        vec2 next_pos = next_tile->get<Transform>().position;

        // Calculate path segment
        vec2 path_vec = {next_pos.x - current_pos.x,
                         next_pos.y - current_pos.y};
        float path_len = vec::length(path_vec);

        if (path_len < EPSILON) {
            // Tiles are co-located, just go to next
            vec2 dir = {next_pos.x - t.position.x, next_pos.y - t.position.y};
            if (vec::length(dir) > EPSILON) {
                steering.path_direction = vec::norm(dir);
            } else {
                steering.path_direction = {0, 0};
            }
            return;
        }

        vec2 path_dir = {path_vec.x / path_len, path_vec.y / path_len};

        // Find closest point on path segment
        vec2 to_agent = {t.position.x - current_pos.x,
                         t.position.y - current_pos.y};
        float projection = to_agent.x * path_dir.x + to_agent.y * path_dir.y;
        projection = std::max(0.0f, std::min(path_len, projection));

        vec2 closest_on_path = {current_pos.x + path_dir.x * projection,
                                current_pos.y + path_dir.y * projection};

        // How far are we from the path?
        float dist_from_path = vec::distance(t.position, closest_on_path);

        // Blend: pull toward path + move along path
        constexpr float PATH_PULL_RADIUS =
            0.5f;  // Pull toward path within this distance (half a tile)

        vec2 final_dir;
        if (dist_from_path > PATH_PULL_RADIUS) {
            // Too far - move toward the path
            vec2 to_path = {closest_on_path.x - t.position.x,
                            closest_on_path.y - t.position.y};
            final_dir = vec::norm(to_path);
        } else {
            // Close enough - move along path toward next tile
            vec2 to_next = {next_pos.x - t.position.x,
                            next_pos.y - t.position.y};
            final_dir = vec::norm(to_next);
        }

        if (vec::length(final_dir) > EPSILON) {
            steering.path_direction = vec::norm(final_dir);
        } else {
            steering.path_direction = {0, 0};
        }
    }
};

// Calculate separation forces
struct SeparationSystem : System<Transform, Agent, AgentSteering> {
    static constexpr float SEPARATION_RADIUS =
        0.4f;  // Smaller radius - only push when very close

    void for_each_with(Entity& e, Transform& t, Agent&, AgentSteering& steering,
                       float) override {
        vec2 force{0, 0};

        auto nearby = EntityQuery()
                          .whereHasComponent<Transform>()
                          .whereHasComponent<Agent>()
                          .whereNotID(e.id)
                          .whereLambda([&t](const Entity& other) {
                              float dist = vec::distance(
                                  t.position, other.get<Transform>().position);
                              return dist < SEPARATION_RADIUS && dist > EPSILON;
                          })
                          .gen();

        for (const Entity& other : nearby) {
            const Transform& other_t = other.get<Transform>();
            float dist = vec::distance(t.position, other_t.position);
            vec2 away = {t.position.x - other_t.position.x,
                         t.position.y - other_t.position.y};
            away = vec::norm(away);
            float strength =
                (1.0f - (dist / SEPARATION_RADIUS)) * 1.0f;  // Weaker force
            force.x += away.x * strength;
            force.y += away.y * strength;
        }

        steering.separation = force;
    }
};

// Combine steering forces into final velocity - SIMPLE version
struct VelocityCombineSystem : System<Transform, AgentSteering> {
    static constexpr float MOVE_SPEED = 3.0f;
    static constexpr float SEPARATION_WEIGHT = 0.2f;

    void for_each_with(Entity&, Transform& t, AgentSteering& steering,
                       float) override {
        vec2 move_dir = {steering.path_direction.x +
                             steering.separation.x * SEPARATION_WEIGHT,
                         steering.path_direction.y +
                             steering.separation.y * SEPARATION_WEIGHT};

        float move_len = vec::length(move_dir);
        if (move_len > EPSILON) {
            // Snap to cardinal direction (no diagonal movement)
            if (std::abs(move_dir.x) > std::abs(move_dir.y)) {
                move_dir = {move_dir.x > 0 ? 1.f : -1.f, 0.f};
            } else {
                move_dir = {0.f, move_dir.y > 0 ? 1.f : -1.f};
            }
            t.velocity = {move_dir.x * MOVE_SPEED, move_dir.y * MOVE_SPEED};
        } else {
            t.velocity = {0, 0};
        }
    }
};

struct MovementSystem : System<Transform> {
    void for_each_with(Entity&, Transform& t, float dt) override {
        t.position = t.position + t.velocity * dt;
    }
};

struct AgentLifetimeSystem : System<Agent> {
    void for_each_with(Entity&, Agent& a, float dt) override {
        a.time_alive += dt;
    }
};

struct AttractionSpawnSystem : System<Transform, Attraction> {
    FacilityType random_want() {
        int r = rand() % 100;
        if (r < 40) return FacilityType::Food;
        if (r < 70) return FacilityType::Bathroom;
        return FacilityType::Stage;
    }

    void for_each_with(Entity& e, Transform& t, Attraction& a,
                       float dt) override {
        // Don't spawn during game over
        auto* state = EntityHelper::get_singleton_cmp<GameState>();
        if (state && state->is_game_over()) return;

        a.spawn_timer += dt;

        float spawn_interval = 1.0f / a.spawn_rate;
        while (a.spawn_timer >= spawn_interval &&
               a.current_count < a.capacity) {
            a.spawn_timer -= spawn_interval;

            Entity& agent = EntityHelper::createEntity();
            float offset_x = ((float) (rand() % 100) / 100.f - 0.5f) * 0.5f;
            float offset_z = ((float) (rand() % 100) / 100.f - 0.5f) * 0.5f;
            agent.addComponent<Transform>(t.position.x + offset_x,
                                          t.position.y + offset_z);
            Agent& ag = agent.addComponent<Agent>();
            ag.origin_id = e.id;
            ag.want = random_want();
            agent.addComponent<HasStress>();

            // Components for targeting and steering
            agent.addComponent<AgentTarget>();
            agent.addComponent<AgentSteering>();

            a.current_count++;
        }
    }
};

// Manages stage state machine: Idle -> Announcing -> Performing -> Clearing ->
// Idle
struct StageManagementSystem : System<Facility, StageInfo> {
    void for_each_with(Entity& stage_entity, Facility& f, StageInfo& info,
                       float dt) override {
        info.state_timer += dt;

        switch (info.state) {
            case StageState::Idle:
                if (info.state_timer >= info.idle_duration) {
                    info.state = StageState::Announcing;
                    info.state_timer = 0.f;
                    // Could randomize artist popularity here
                    info.artist_popularity = 0.3f + (rand() % 70) / 100.f;
                }
                break;

            case StageState::Announcing:
                if (info.state_timer >= info.announce_duration) {
                    info.state = StageState::Performing;
                    info.state_timer = 0.f;
                }
                break;

            case StageState::Performing:
                if (info.state_timer >= info.perform_duration) {
                    info.state = StageState::Clearing;
                    info.state_timer = 0.f;
                }
                break;

            case StageState::Clearing:
                // Force all agents inside this stage to leave
                if (info.state_timer >= info.clear_duration) {
                    info.state = StageState::Idle;
                    info.state_timer = 0.f;
                    f.current_occupants = 0;  // Reset occupancy
                }
                break;
        }
    }
};

// Forces agents to leave stage when it's clearing
struct StageClearingSystem : System<Agent, InsideFacility> {
    void for_each_with(Entity& agent, Agent& a, InsideFacility& inside,
                       float) override {
        if (a.want != FacilityType::Stage) return;

        auto stage_opt = EntityHelper::getEntityForID(inside.facility_id);
        if (!stage_opt.valid() || !stage_opt->has<StageInfo>()) return;

        const StageInfo& info = stage_opt->get<StageInfo>();

        // If stage is clearing, kick everyone out immediately
        if (info.state == StageState::Clearing) {
            inside.service_time = 0.f;  // Will trigger exit next frame
        }
    }
};

struct FacilityAbsorptionSystem : System<Transform, Facility> {
    static constexpr float ABSORPTION_RADIUS = 1.5f;
    static constexpr float FACILITY_SIZE = 1.5f;

    void for_each_with(Entity& facility_entity, Transform& f_t, Facility& f,
                       float dt) override {
        f.absorption_timer += dt;

        // Check if this is a stage and if it's accepting people
        bool is_stage = f.type == FacilityType::Stage;
        bool stage_open = true;
        float stage_remaining_time = 0.f;

        if (is_stage && facility_entity.has<StageInfo>()) {
            const StageInfo& info = facility_entity.get<StageInfo>();
            // Only let people in during Announcing or Performing
            stage_open = (info.state == StageState::Announcing ||
                          info.state == StageState::Performing);

            // Calculate how much time is left in the performance
            if (info.state == StageState::Performing) {
                stage_remaining_time = info.perform_duration - info.state_timer;
            } else if (info.state == StageState::Announcing) {
                // If announcing, they'll stay for the whole performance
                stage_remaining_time =
                    (info.announce_duration - info.state_timer) +
                    info.perform_duration;
            }
        }

        if (is_stage && !stage_open) return;  // Stage not accepting people

        float absorb_interval = 1.0f / f.absorption_rate;

        // Find agents walking toward this facility (not already inside one)
        auto agents =
            EntityQuery()
                .whereHasComponent<Transform>()
                .whereHasComponent<Agent>()
                .whereMissingComponent<InsideFacility>()
                .whereLambda([&f, &f_t](const Entity& agent) {
                    if (agent.get<Agent>().want != f.type) return false;
                    float dist = vec::distance(f_t.position,
                                               agent.get<Transform>().position);
                    return dist < ABSORPTION_RADIUS;
                })
                .orderByLambda([&f_t](const Entity& a, const Entity& b) {
                    float dist_a = vec::distance(f_t.position,
                                                 a.get<Transform>().position);
                    float dist_b = vec::distance(f_t.position,
                                                 b.get<Transform>().position);
                    return dist_a < dist_b;
                })
                .gen();

        // Absorb the nearest agent if timer allows
        if (agents.empty() || f.current_occupants >= f.capacity) return;
        if (f.absorption_timer < absorb_interval) return;

        Entity& agent = agents.front();
        Transform& a_t = agent.get<Transform>();

        f.absorption_timer -= absorb_interval;
        f.current_occupants++;

        // Calculate a slot position inside the facility
        int slot = f.current_occupants - 1;
        int cols = (int) std::ceil(std::sqrt((float) f.capacity));
        int row = slot / cols;
        int col = slot % cols;
        float cell_size = FACILITY_SIZE / (float) cols;
        float start_offset = -FACILITY_SIZE * 0.5f + cell_size * 0.5f;

        // Add InsideFacility component - agent stays visible inside
        InsideFacility& inside = agent.addComponent<InsideFacility>();
        inside.facility_id = facility_entity.id;
        inside.time_inside = 0.f;

        // Stage: stay for the whole performance; others: 2-4 seconds
        if (is_stage) {
            inside.service_time = stage_remaining_time + 1.0f;  // +1 buffer
        } else {
            inside.service_time = 2.0f + (rand() % 100) / 50.f;
        }

        inside.slot_offset = {start_offset + col * cell_size,
                              start_offset + row * cell_size};

        // Move agent to facility position (will be offset in render)
        a_t.position = f_t.position;
        a_t.velocity = {0, 0};

        // Clear steering so they stop moving
        if (agent.has<AgentSteering>()) {
            AgentSteering& steer = agent.get<AgentSteering>();
            steer.path_direction = {0, 0};
            steer.separation = {0, 0};
        }
    }
};

// Handle agents exiting facilities after service time - they get a new activity
struct FacilityExitSystem
    : System<Transform, Agent, InsideFacility, AgentTarget, HasStress> {
    FacilityType random_next_want(FacilityType current) {
        // Pick a new want, weighted by common needs
        // Avoid picking the same thing twice in a row
        int r = rand() % 100;

        if (current == FacilityType::Stage) {
            // After watching a show, probably need food or bathroom
            if (r < 50) return FacilityType::Food;
            if (r < 80) return FacilityType::Bathroom;
            return FacilityType::Stage;  // Some people want more music!
        } else if (current == FacilityType::Food) {
            // After eating, might want bathroom or entertainment
            if (r < 40) return FacilityType::Bathroom;
            if (r < 80) return FacilityType::Stage;
            return FacilityType::Food;  // Still hungry
        } else {                        // Bathroom
            // After bathroom, probably want food or entertainment
            if (r < 50) return FacilityType::Stage;
            if (r < 80) return FacilityType::Food;
            return FacilityType::Bathroom;
        }
    }

    void for_each_with(Entity& agent, Transform& t, Agent& a,
                       InsideFacility& inside, AgentTarget& target,
                       HasStress& stress, float dt) override {
        inside.time_inside += dt;

        if (inside.time_inside >= inside.service_time) {
            // Get the facility to update occupancy and get exit position
            auto facility_opt =
                EntityHelper::getEntityForID(inside.facility_id);
            if (facility_opt.valid() && facility_opt->has<Facility>() &&
                facility_opt->has<Transform>()) {
                Facility& f = facility_opt->get<Facility>();
                const Transform& f_t = facility_opt->get<Transform>();
                f.current_occupants = std::max(0, f.current_occupants - 1);

                // Move agent to facility entrance (offset from center)
                t.position = {f_t.position.x - 1.0f, f_t.position.y};
            }

            // Completing an activity reduces stress significantly
            stress.stress = std::max(0.f, stress.stress - 0.3f);

            // Assign new activity
            a.want = random_next_want(a.want);

            // Reset target so TargetFindingSystem finds a new one
            target.facility_id = -1;
            target.target_pos = {0, 0};

            // Remove InsideFacility component - agent is back in the world
            agent.removeComponent<InsideFacility>();
        }
    }
};

// Counts agents near each path segment and updates congestion
struct PathCongestionSystem : System<Transform, PathTile> {
    void once(float) override {
        // Reset all path loads at the start of each frame
        auto tiles = EntityQuery().whereHasComponent<PathTile>().gen();
        for (Entity& tile : tiles) {
            tile.get<PathTile>().current_load = 0.f;
        }
    }

    void for_each_with(Entity&, Transform& tile_t, PathTile& tile,
                       float) override {
        // Count agents within this tile's area (half a tile radius)
        float half_tile = TILESIZE * 0.5f;

        tile.current_load =
            (float) EntityQuery()
                .whereHasComponent<Transform>()
                .whereHasComponent<Agent>()
                .whereMissingComponent<InsideFacility>()
                .whereLambda([&](const Entity& agent) {
                    vec2 agent_pos = agent.get<Transform>().position;
                    float dx = std::abs(agent_pos.x - tile_t.position.x);
                    float dz = std::abs(agent_pos.y - tile_t.position.y);
                    return dx <= half_tile && dz <= half_tile;
                })
                .gen_count();
    }
};

struct StressBuildupSystem : System<Transform, Agent, HasStress> {
    static constexpr float CROWDING_RADIUS = 1.5f;
    static constexpr int CROWDING_THRESHOLD = 5;
    static constexpr float TIME_STRESS_THRESHOLD = 15.f;
    static constexpr float CONGESTION_STRESS_RATE = 0.15f;

    // Check if a position is on a path tile
    static bool is_on_path_tile(const vec2& pos, const Entity& tile) {
        const Transform& tile_t = tile.get<Transform>();
        float half_tile = TILESIZE * 0.5f;
        float dx = std::abs(pos.x - tile_t.position.x);
        float dz = std::abs(pos.y - tile_t.position.y);
        return dx <= half_tile && dz <= half_tile;
    }

    // Find the path tile this agent is on and return its congestion ratio
    float get_path_congestion(const vec2& pos) {
        auto tiles = EntityQuery()
                         .whereHasComponent<Transform>()
                         .whereHasComponent<PathTile>()
                         .whereLambda([&pos](const Entity& tile) {
                             return is_on_path_tile(pos, tile);
                         })
                         .orderByLambda([](const Entity& a, const Entity& b) {
                             return a.get<PathTile>().congestion_ratio() >
                                    b.get<PathTile>().congestion_ratio();
                         })
                         .gen();

        if (tiles.empty()) return 0.f;
        return tiles.front().get().get<PathTile>().congestion_ratio();
    }

    void for_each_with(Entity& e, Transform& t, Agent& a, HasStress& s,
                       float dt) override {
        // Agents inside facilities don't accumulate crowding stress
        if (e.has<InsideFacility>()) return;

        float stress_delta = 0.f;

        // Crowding stress from nearby agents
        int nearby_count =
            (int) EntityQuery()
                .whereHasComponent<Transform>()
                .whereHasComponent<Agent>()
                .whereNotID(e.id)
                .whereMissingComponent<InsideFacility>()
                .whereLambda([&t](const Entity& other) {
                    float dist = vec::distance(t.position,
                                               other.get<Transform>().position);
                    return dist < CROWDING_RADIUS;
                })
                .gen_count();

        if (nearby_count > CROWDING_THRESHOLD) {
            stress_delta += (nearby_count - CROWDING_THRESHOLD) * 0.1f;
        }

        // Time-based stress
        if (a.time_alive > TIME_STRESS_THRESHOLD) {
            stress_delta += (a.time_alive - TIME_STRESS_THRESHOLD) * 0.02f;
        }

        // Path congestion stress - builds when on overcrowded paths
        float congestion = get_path_congestion(t.position);
        if (congestion > 1.0f) {
            // Stress scales with how overcrowded the path is
            stress_delta += (congestion - 1.0f) * CONGESTION_STRESS_RATE;
        }

        s.stress += stress_delta * dt;
        s.stress = std::clamp(s.stress, 0.f, 1.f);
    }
};

struct StressSpreadSystem : System<Transform, HasStress> {
    static constexpr float SPREAD_RADIUS = 2.0f;
    static constexpr float SPREAD_RATE = 0.1f;

    void for_each_with(Entity& e, Transform& t, HasStress& s,
                       float dt) override {
        if (s.stress < 0.1f) return;

        auto nearby = EntityQuery()
                          .whereHasComponent<Transform>()
                          .whereHasComponent<HasStress>()
                          .whereNotID(e.id)
                          .whereLambda([&t](const Entity& other) {
                              float dist = vec::distance(
                                  t.position, other.get<Transform>().position);
                              return dist < SPREAD_RADIUS && dist > EPSILON;
                          })
                          .gen();

        for (Entity& other : nearby) {
            float dist =
                vec::distance(t.position, other.get<Transform>().position);
            HasStress& other_s = other.get<HasStress>();
            float spread_amount =
                s.stress * SPREAD_RATE * (1.f - dist / SPREAD_RADIUS) * dt;
            other_s.stress = std::min(other_s.stress + spread_amount, 1.f);
        }
    }
};

void register_update_systems(SystemManager& sm) {
    // Game state tracking (must run first)
    sm.register_update_system(std::make_unique<GameStateSystem>());

    // Core systems
    sm.register_update_system(std::make_unique<CameraInputSystem>());

    // Path builder systems (mouse picking, staging, confirmation)
    sm.register_update_system(std::make_unique<MousePickingSystem>());
    sm.register_update_system(std::make_unique<PathStagingSystem>());
    sm.register_update_system(std::make_unique<PathConfirmationSystem>());

    sm.register_update_system(std::make_unique<AttractionSpawnSystem>());

    // Facility state machines
    sm.register_update_system(std::make_unique<StageManagementSystem>());
    sm.register_update_system(std::make_unique<StageClearingSystem>());

    // Target finding and path following
    sm.register_update_system(std::make_unique<TargetFindingSystem>());
    sm.register_update_system(std::make_unique<PathFollowingSystem>());

    // Movement pipeline
    sm.register_update_system(std::make_unique<SeparationSystem>());
    sm.register_update_system(std::make_unique<VelocityCombineSystem>());
    sm.register_update_system(std::make_unique<MovementSystem>());

    // Agent lifecycle
    sm.register_update_system(std::make_unique<AgentLifetimeSystem>());
    sm.register_update_system(std::make_unique<FacilityAbsorptionSystem>());
    sm.register_update_system(std::make_unique<FacilityExitSystem>());

    // Path congestion tracking (must run before stress)
    sm.register_update_system(std::make_unique<PathCongestionSystem>());

    // Stress systems
    sm.register_update_system(std::make_unique<StressBuildupSystem>());
    sm.register_update_system(std::make_unique<StressSpreadSystem>());
}
