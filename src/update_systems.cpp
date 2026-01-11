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
        vec2 goal_pull = calculate_goal_pull(t);
        
        vec2 acceleration = {
            separation.x * b.separation_weight + goal_pull.x * b.goal_pull_weight,
            separation.y * b.separation_weight + goal_pull.y * b.goal_pull_weight
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

void register_update_systems(SystemManager& sm) {
    sm.register_update_system(std::make_unique<CameraInputSystem>());
    sm.register_update_system(std::make_unique<AttractionSpawnSystem>());
    sm.register_update_system(std::make_unique<BoidsSystem>());
    sm.register_update_system(std::make_unique<MovementSystem>());
    sm.register_update_system(std::make_unique<AgentLifetimeSystem>());
    sm.register_update_system(std::make_unique<FacilityAbsorptionSystem>());
}
