#include "afterhours/src/core/entity_query.h"
#include "components.h"
#include "systems.h"
#include "vec_util.h"

struct CameraInputSystem : System<ProvidesCamera> {
    void for_each_with(Entity&, ProvidesCamera& cam, float dt) override {
        cam.cam.handle_input(dt);
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

// Follow signposts - find closest node, go to next node, stay on path
struct PathFollowingSystem
    : System<Transform, Agent, AgentTarget, AgentSteering> {
    void for_each_with(Entity&, Transform& t, Agent& a,
                       AgentTarget& agent_target, AgentSteering& steering,
                       float) override {
        // Find closest PathNode - but prefer nodes whose NEXT node is closer to
        // our target This prevents oscillation at hub nodes
        auto score_node = [&](const Entity& node) {
            const Transform& node_t = node.get<Transform>();
            float dist = vec::distance(t.position, node_t.position);
            const PathSignpost& sp = node.get<PathSignpost>();
            int next_id = sp.get_next_node(a.want);
            float score = dist;
            if (next_id >= 0 && agent_target.facility_id >= 0) {
                auto next_node = EntityHelper::getEntityForID(next_id);
                if (next_node.valid() && next_node->has<Transform>()) {
                    float next_to_target =
                        vec::distance(next_node->get<Transform>().position,
                                      agent_target.target_pos);
                    score = dist + next_to_target * 0.1f;
                }
            }
            return score;
        };

        auto path_nodes =
            EntityQuery()
                .whereHasComponent<Transform>()
                .whereHasComponent<PathNode>()
                .whereHasComponent<PathSignpost>()
                .orderByLambda([&](const Entity& a, const Entity& b) {
                    return score_node(a) < score_node(b);
                })
                .gen();

        if (path_nodes.empty()) {
            steering.path_direction = {0, 0};
            return;
        }

        int closest_node_id = path_nodes.front().id;

        // Ask this node: "where do I go next?"
        auto closest = EntityHelper::getEntityForID(closest_node_id);
        if (!closest.valid()) {
            steering.path_direction = {0, 0};
            return;
        }

        const PathSignpost& signpost = closest->get<PathSignpost>();
        int next_node_id = signpost.get_next_node(a.want);

        // -1 means we're at the destination
        if (next_node_id < 0) {
            // Go directly to facility
            if (agent_target.facility_id >= 0) {
                vec2 dir = {agent_target.target_pos.x - t.position.x,
                            agent_target.target_pos.y - t.position.y};
                if (vec::length(dir) > EPSILON) {
                    steering.path_direction = vec::norm(dir);
                    return;
                }
            }
            steering.path_direction = {0, 0};
            return;
        }

        // Go toward the next node, but stay close to the path
        auto next_node = EntityHelper::getEntityForID(next_node_id);
        auto current_node = EntityHelper::getEntityForID(closest_node_id);
        if (!next_node.valid() || !next_node->has<Transform>() ||
            !current_node.valid()) {
            steering.path_direction = {0, 0};
            return;
        }

        vec2 current_pos = current_node->get<Transform>().position;
        vec2 next_pos = next_node->get<Transform>().position;

        // Calculate path segment
        vec2 path_vec = {next_pos.x - current_pos.x,
                         next_pos.y - current_pos.y};
        float path_len = vec::length(path_vec);

        if (path_len < EPSILON) {
            // Nodes are co-located, just go to next
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
            0.3f;  // Pull toward path within this distance

        vec2 final_dir;
        if (dist_from_path > PATH_PULL_RADIUS) {
            // Too far - move toward the path
            vec2 to_path = {closest_on_path.x - t.position.x,
                            closest_on_path.y - t.position.y};
            final_dir = vec::norm(to_path);
        } else {
            // Close enough - move along path toward next node
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
    static constexpr float SEPARATION_WEIGHT = 0.2f;  // Light separation

    void for_each_with(Entity&, Transform& t, AgentSteering& steering,
                       float) override {
        // Path direction is primary, separation is secondary
        vec2 move_dir = {steering.path_direction.x +
                             steering.separation.x * SEPARATION_WEIGHT,
                         steering.path_direction.y +
                             steering.separation.y * SEPARATION_WEIGHT};

        float move_len = vec::length(move_dir);
        if (move_len > EPSILON) {
            move_dir = vec::norm(move_dir);
            t.velocity.x = move_dir.x * MOVE_SPEED;
            t.velocity.y = move_dir.y * MOVE_SPEED;
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

struct StressBuildupSystem : System<Transform, Agent, HasStress> {
    static constexpr float CROWDING_RADIUS = 1.5f;
    static constexpr int CROWDING_THRESHOLD = 5;
    static constexpr float TIME_STRESS_THRESHOLD = 15.f;

    void for_each_with(Entity& e, Transform& t, Agent& a, HasStress& s,
                       float dt) override {
        // Agents inside facilities don't accumulate crowding stress
        if (e.has<InsideFacility>()) return;

        float stress_delta = 0.f;

        auto nearby = EntityQuery()
                          .whereHasComponent<Transform>()
                          .whereHasComponent<Agent>()
                          .whereNotID(e.id)
                          .whereMissingComponent<InsideFacility>()
                          .whereLambda([&t](const Entity& other) {
                              float dist = vec::distance(
                                  t.position, other.get<Transform>().position);
                              return dist < CROWDING_RADIUS;
                          })
                          .gen();

        int nearby_count = (int) nearby.size();

        if (nearby_count > CROWDING_THRESHOLD) {
            stress_delta += (nearby_count - CROWDING_THRESHOLD) * 0.1f;
        }

        if (a.time_alive > TIME_STRESS_THRESHOLD) {
            stress_delta += (a.time_alive - TIME_STRESS_THRESHOLD) * 0.02f;
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
    // Core systems
    sm.register_update_system(std::make_unique<CameraInputSystem>());
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

    // Stress systems
    sm.register_update_system(std::make_unique<StressBuildupSystem>());
    sm.register_update_system(std::make_unique<StressSpreadSystem>());
}
