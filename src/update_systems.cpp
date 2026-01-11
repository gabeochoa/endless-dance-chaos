#include "systems.h"
#include "components.h"
#include "vec_util.h"
#include "afterhours/src/core/entity_query.h"

struct CameraInputSystem : System<ProvidesCamera> {
    void for_each_with(Entity&, ProvidesCamera& cam, float dt) override {
        cam.cam.handle_input(dt);
    }
};

struct BoidsSystem : System<Transform, Agent, BoidsBehavior> {
    static constexpr float SEPARATION_RADIUS = 1.5f;
    static constexpr float GOAL_RADIUS = 0.5f;
    static constexpr float PATH_ATTRACTION_RADIUS = 3.0f;

    vec2 calculate_separation(const Entity& self, const Transform& t) {
        vec2 force{0, 0};
        
        auto nearby = EntityQuery()
            .whereHasComponent<Transform>()
            .whereHasComponent<Agent>()
            .whereNotID(self.id)
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
                float strength = 1.0f - (dist / SEPARATION_RADIUS);
                force.x += away.x * strength;
                force.y += away.y * strength;
            }
        }
        
        return force;
    }

    vec2 closest_point_on_segment(vec2 p, vec2 a, vec2 b) {
        vec2 ab = {b.x - a.x, b.y - a.y};
        float ab_len_sq = ab.x * ab.x + ab.y * ab.y;
        if (ab_len_sq < EPSILON) return a;
        
        vec2 ap = {p.x - a.x, p.y - a.y};
        float t = (ap.x * ab.x + ap.y * ab.y) / ab_len_sq;
        t = std::clamp(t, 0.f, 1.f);
        
        return {a.x + t * ab.x, a.y + t * ab.y};
    }

    vec2 calculate_path_follow(const Transform& t) {
        vec2 force{0, 0};
        float closest_dist = std::numeric_limits<float>::max();
        vec2 closest_point{0, 0};
        vec2 path_direction{0, 0};
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
            
            vec2 cp = closest_point_on_segment(t.position, a_t.position, b_t.position);
            float dist = vec::distance(t.position, cp);
            
            if (dist < closest_dist && dist < node.width) {
                closest_dist = dist;
                closest_point = cp;
                path_direction = vec::norm({
                    b_t.position.x - a_t.position.x,
                    b_t.position.y - a_t.position.y
                });
                found = true;
            }
        }
        
        if (found) {
            vec2 toward_path = {
                closest_point.x - t.position.x,
                closest_point.y - t.position.y
            };
            if (closest_dist > 0.1f) {
                toward_path = vec::norm(toward_path);
                force.x = toward_path.x * 0.5f + path_direction.x * 0.5f;
                force.y = toward_path.y * 0.5f + path_direction.y * 0.5f;
            } else {
                force = path_direction;
            }
        }
        
        return force;
    }

    vec2 calculate_goal_pull(const Transform& t) {
        vec2 force{0, 0};
        
        auto facilities = EntityQuery()
            .whereHasComponent<Transform>()
            .whereHasComponent<Facility>()
            .gen();
        
        float closest_dist = std::numeric_limits<float>::max();
        vec2 closest_pos{0, 0};
        bool found = false;
        
        for (const Entity& facility : facilities) {
            const Transform& f_t = facility.get<Transform>();
            float dist = vec::distance(t.position, f_t.position);
            if (dist < closest_dist) {
                closest_dist = dist;
                closest_pos = f_t.position;
                found = true;
            }
        }
        
        if (found && closest_dist > GOAL_RADIUS) {
            vec2 toward = {
                closest_pos.x - t.position.x,
                closest_pos.y - t.position.y
            };
            force = vec::norm(toward);
        }
        
        return force;
    }

    void for_each_with(Entity& e, Transform& t, Agent&, BoidsBehavior& b, float dt) override {
        vec2 separation = calculate_separation(e, t);
        vec2 path_follow = calculate_path_follow(t);
        vec2 goal_pull = calculate_goal_pull(t);
        
        vec2 acceleration = {
            separation.x * b.separation_weight + 
            path_follow.x * b.path_follow_weight +
            goal_pull.x * b.goal_pull_weight,
            separation.y * b.separation_weight + 
            path_follow.y * b.path_follow_weight +
            goal_pull.y * b.goal_pull_weight
        };
        
        t.velocity.x += acceleration.x * dt;
        t.velocity.y += acceleration.y * dt;
        
        float speed = vec::length(t.velocity);
        if (speed > b.max_speed) {
            t.velocity = vec::norm(t.velocity);
            t.velocity.x *= b.max_speed;
            t.velocity.y *= b.max_speed;
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
    void for_each_with(Entity& e, Transform& t, Attraction& a, float dt) override {
        a.spawn_timer += dt;
        
        float spawn_interval = 1.0f / a.spawn_rate;
        while (a.spawn_timer >= spawn_interval && a.current_count < a.capacity) {
            a.spawn_timer -= spawn_interval;
            
            Entity& agent = EntityHelper::createEntity();
            float offset_x = ((float)(rand() % 100) / 100.f - 0.5f) * 0.5f;
            float offset_z = ((float)(rand() % 100) / 100.f - 0.5f) * 0.5f;
            agent.addComponent<Transform>(t.position.x + offset_x, t.position.y + offset_z);
            agent.addComponent<Agent>().origin_id = e.id;
            agent.addComponent<BoidsBehavior>();
            agent.addComponent<HasStress>();
            
            a.current_count++;
        }
    }
};

struct FacilityAbsorptionSystem : System<Transform, Facility> {
    static constexpr float ABSORPTION_RADIUS = 1.0f;

    void for_each_with(Entity& facility, Transform& f_t, Facility& f, float dt) override {
        f.absorption_timer += dt;
        
        float absorb_interval = 1.0f / f.absorption_rate;
        if (f.absorption_timer < absorb_interval) return;
        
        auto agents = EntityQuery()
            .whereHasComponent<Transform>()
            .whereHasComponent<Agent>()
            .gen();
        
        for (Entity& agent : agents) {
            Transform& a_t = agent.get<Transform>();
            float dist = vec::distance(f_t.position, a_t.position);
            
            if (dist < ABSORPTION_RADIUS) {
                f.absorption_timer -= absorb_interval;
                
                Agent& a = agent.get<Agent>();
                auto origin = EntityHelper::getEntityForID(a.origin_id);
                if (origin.valid() && origin->has<Attraction>()) {
                    origin->get<Attraction>().current_count--;
                }
                
                EntityHelper::markIDForCleanup(agent.id);
                break;
            }
        }
    }
};

struct StressBuildupSystem : System<Transform, Agent, HasStress> {
    static constexpr float CROWDING_RADIUS = 2.0f;
    static constexpr int CROWDING_THRESHOLD = 2;
    static constexpr float TIME_STRESS_THRESHOLD = 3.f;

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
            stress_delta += (nearby_count - CROWDING_THRESHOLD) * 0.5f;
        }
        
        if (a.time_alive > TIME_STRESS_THRESHOLD) {
            stress_delta += (a.time_alive - TIME_STRESS_THRESHOLD) * 0.1f;
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

struct StressEffectsSystem : System<HasStress, BoidsBehavior> {
    void for_each_with(Entity&, HasStress& s, BoidsBehavior& b, float) override {
        float stress = s.stress;
        
        b.separation_weight = 1.0f * (1.f - stress * 0.5f);
        b.path_follow_weight = 2.0f * (1.f - stress * 0.7f);
        b.goal_pull_weight = 2.0f + stress * 1.5f;
        b.max_speed = 4.0f + stress * 2.0f;
    }
};

void register_update_systems(SystemManager& sm) {
    sm.register_update_system(std::make_unique<CameraInputSystem>());
    sm.register_update_system(std::make_unique<AttractionSpawnSystem>());
    sm.register_update_system(std::make_unique<StressBuildupSystem>());
    sm.register_update_system(std::make_unique<StressSpreadSystem>());
    sm.register_update_system(std::make_unique<StressEffectsSystem>());
    sm.register_update_system(std::make_unique<BoidsSystem>());
    sm.register_update_system(std::make_unique<MovementSystem>());
    sm.register_update_system(std::make_unique<AgentLifetimeSystem>());
    sm.register_update_system(std::make_unique<FacilityAbsorptionSystem>());
}
