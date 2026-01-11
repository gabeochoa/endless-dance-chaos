#pragma once

#include "components.h"
#include "vec_util.h"
#include "afterhours/src/core/system.h"
#include "afterhours/src/core/entity_helper.h"

using namespace afterhours;

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

inline void register_update_systems(SystemManager& sm) {
    sm.register_update_system(std::make_unique<MovementSystem>());
    sm.register_update_system(std::make_unique<AgentLifetimeSystem>());
}

inline void register_render_systems(SystemManager& sm) {
    (void)sm;
}
