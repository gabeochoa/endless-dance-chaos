#include "systems.h"
#include "components.h"
#include "vec_util.h"
#include "afterhours/src/core/entity_query.h"

struct CameraInputSystem : System<ProvidesCamera> {
    void for_each_with(Entity&, ProvidesCamera& cam, float dt) override {
        cam.cam.handle_input(dt);
    }
};

// Find the nearest Facility matching the agent's want
struct TargetFindingSystem : System<Transform, Agent, AgentTarget> {
    void for_each_with(Entity&, Transform& t, Agent& a, AgentTarget& target, float) override {
        // Skip if we already have a valid target
        if (target.facility_id >= 0) {
            auto existing = EntityHelper::getEntityForID(target.facility_id);
            if (existing.valid() && existing->has<Facility>()) {
                // Check if facility still matches our want
                if (existing->get<Facility>().type == a.want) {
                    return;  // Target is still valid
                }
            }
        }
        
        // Find nearest facility matching our want
        float best_dist = std::numeric_limits<float>::max();
        int best_id = -1;
        vec2 best_pos{0, 0};
        
        auto facilities = EntityQuery()
            .whereHasComponent<Transform>()
            .whereHasComponent<Facility>()
            .gen();
        
        for (const Entity& facility : facilities) {
            const Facility& f = facility.get<Facility>();
            if (f.type != a.want) continue;
            
            const Transform& f_t = facility.get<Transform>();
            float dist = vec::distance(t.position, f_t.position);
            
            if (dist < best_dist) {
                best_dist = dist;
                best_id = facility.id;
                best_pos = f_t.position;
            }
        }
        
        target.facility_id = best_id;
        target.target_pos = best_pos;
    }
};

// Follow PathNode chain - find closest path segment and move toward next node
struct PathFollowingSystem : System<Transform, AgentSteering> {
    vec2 closest_point_on_segment(vec2 p, vec2 a, vec2 b, float& t_out) {
        vec2 ab = {b.x - a.x, b.y - a.y};
        float ab_len_sq = ab.x * ab.x + ab.y * ab.y;
        if (ab_len_sq < EPSILON) {
            t_out = 0.f;
            return a;
        }
        
        vec2 ap = {p.x - a.x, p.y - a.y};
        float t = (ap.x * ab.x + ap.y * ab.y) / ab_len_sq;
        t = std::clamp(t, 0.f, 1.f);
        t_out = t;
        
        return {a.x + t * ab.x, a.y + t * ab.y};
    }

    void for_each_with(Entity&, Transform& t, AgentSteering& steering, float) override {
        float closest_dist = std::numeric_limits<float>::max();
        vec2 target_point{0, 0};
        bool found = false;
        
        auto path_nodes = EntityQuery()
            .whereHasComponent<Transform>()
            .whereHasComponent<PathNode>()
            .gen();
        
        for (const Entity& node_entity : path_nodes) {
            const PathNode& node = node_entity.get<PathNode>();
            if (node.next_node_id < 0) continue;
            
            auto next = EntityHelper::getEntityForID(node.next_node_id);
            if (!next.valid() || !next->has<Transform>()) continue;
            
            const Transform& a_t = node_entity.get<Transform>();
            const Transform& b_t = next->get<Transform>();
            
            float seg_t;
            vec2 cp = closest_point_on_segment(t.position, a_t.position, b_t.position, seg_t);
            float dist = vec::distance(t.position, cp);
            
            if (dist < closest_dist) {
                closest_dist = dist;
                target_point = b_t.position;
                found = true;
            }
        }
        
        if (found) {
            vec2 dir = {target_point.x - t.position.x, target_point.y - t.position.y};
            float len = vec::length(dir);
            if (len > EPSILON) {
                steering.path_direction = vec::norm(dir);
                return;
            }
        }
        
        steering.path_direction = {0, 0};
    }
};

// Calculate separation forces
struct SeparationSystem : System<Transform, Agent, AgentSteering> {
    static constexpr float SEPARATION_RADIUS = 0.8f;
    
    void for_each_with(Entity& e, Transform& t, Agent&, AgentSteering& steering, float) override {
        vec2 force{0, 0};
        
        auto nearby = EntityQuery()
            .whereHasComponent<Transform>()
            .whereHasComponent<Agent>()
            .whereNotID(e.id)
            .gen();
        
        for (const Entity& other : nearby) {
            const Transform& other_t = other.get<Transform>();
            float dist = vec::distance(t.position, other_t.position);
            
            if (dist < SEPARATION_RADIUS && dist > EPSILON) {
                vec2 away = {
                    t.position.x - other_t.position.x,
                    t.position.y - other_t.position.y
                };
                away = vec::norm(away);
                float strength = (1.0f - (dist / SEPARATION_RADIUS)) * 2.0f;
                force.x += away.x * strength;
                force.y += away.y * strength;
            }
        }
        
        steering.separation = force;
    }
};

// Combine steering forces into final velocity
struct VelocityCombineSystem : System<Transform, AgentSteering> {
    static constexpr float MOVE_SPEED = 3.0f;
    static constexpr float SEPARATION_WEIGHT = 0.3f;
    
    void for_each_with(Entity&, Transform& t, AgentSteering& steering, float) override {
        vec2 move_dir = {
            steering.path_direction.x + steering.separation.x * SEPARATION_WEIGHT,
            steering.path_direction.y + steering.separation.y * SEPARATION_WEIGHT
        };
        
        float move_len = vec::length(move_dir);
        if (move_len > EPSILON) {
            move_dir = vec::norm(move_dir);
        }
        
        t.velocity.x = move_dir.x * MOVE_SPEED;
        t.velocity.y = move_dir.y * MOVE_SPEED;
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

    void for_each_with(Entity& e, Transform& t, Attraction& a, float dt) override {
        a.spawn_timer += dt;
        
        float spawn_interval = 1.0f / a.spawn_rate;
        while (a.spawn_timer >= spawn_interval && a.current_count < a.capacity) {
            a.spawn_timer -= spawn_interval;
            
            Entity& agent = EntityHelper::createEntity();
            float offset_x = ((float)(rand() % 100) / 100.f - 0.5f) * 0.5f;
            float offset_z = ((float)(rand() % 100) / 100.f - 0.5f) * 0.5f;
            agent.addComponent<Transform>(t.position.x + offset_x, t.position.y + offset_z);
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

struct FacilityAbsorptionSystem : System<Transform, Facility> {
    static constexpr float ABSORPTION_RADIUS = 1.5f;

    void for_each_with(Entity&, Transform& f_t, Facility& f, float dt) override {
        f.absorption_timer += dt;
        
        float absorb_interval = 1.0f / f.absorption_rate;
        
        auto agents = EntityQuery()
            .whereHasComponent<Transform>()
            .whereHasComponent<Agent>()
            .gen();
        
        for (Entity& agent : agents) {
            Agent& a = agent.get<Agent>();
            if (a.want != f.type) continue;
            
            Transform& a_t = agent.get<Transform>();
            float dist = vec::distance(f_t.position, a_t.position);
            
            if (dist < ABSORPTION_RADIUS && f.current_occupants < f.capacity) {
                if (f.absorption_timer >= absorb_interval) {
                    f.absorption_timer -= absorb_interval;
                    f.current_occupants++;
                    
                    auto origin = EntityHelper::getEntityForID(a.origin_id);
                    if (origin.valid() && origin->has<Attraction>()) {
                        origin->get<Attraction>().current_count--;
                    }
                    
                    EntityHelper::markIDForCleanup(agent.id);
                }
                break;
            }
        }
        
        if (f.current_occupants > 0) {
            f.current_occupants = std::max(0, f.current_occupants - 1);
        }
    }
};

struct StressBuildupSystem : System<Transform, Agent, HasStress> {
    static constexpr float CROWDING_RADIUS = 1.5f;
    static constexpr int CROWDING_THRESHOLD = 5;
    static constexpr float TIME_STRESS_THRESHOLD = 15.f;

    void for_each_with(Entity& e, Transform& t, Agent& a, HasStress& s, float dt) override {
        float stress_delta = 0.f;
        
        int nearby_count = 0;
        auto nearby = EntityQuery()
            .whereHasComponent<Transform>()
            .whereHasComponent<Agent>()
            .whereNotID(e.id)
            .gen();
        
        for (const Entity& other : nearby) {
            float dist = vec::distance(t.position, other.get<Transform>().position);
            if (dist < CROWDING_RADIUS) {
                nearby_count++;
            }
        }
        
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

    void for_each_with(Entity& e, Transform& t, HasStress& s, float dt) override {
        if (s.stress < 0.1f) return;
        
        auto nearby = EntityQuery()
            .whereHasComponent<Transform>()
            .whereHasComponent<HasStress>()
            .whereNotID(e.id)
            .gen();
        
        for (Entity& other : nearby) {
            float dist = vec::distance(t.position, other.get<Transform>().position);
            if (dist < SPREAD_RADIUS && dist > EPSILON) {
                HasStress& other_s = other.get<HasStress>();
                float spread_amount = s.stress * SPREAD_RATE * (1.f - dist / SPREAD_RADIUS) * dt;
                other_s.stress = std::min(other_s.stress + spread_amount, 1.f);
            }
        }
    }
};

void register_update_systems(SystemManager& sm) {
    // Core systems
    sm.register_update_system(std::make_unique<CameraInputSystem>());
    sm.register_update_system(std::make_unique<AttractionSpawnSystem>());
    
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
    
    // Stress systems
    sm.register_update_system(std::make_unique<StressBuildupSystem>());
    sm.register_update_system(std::make_unique<StressSpreadSystem>());
}
