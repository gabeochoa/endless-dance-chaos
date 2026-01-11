#pragma once

#include "rl.h"
#include "camera.h"
#include "afterhours/src/core/base_component.h"

using namespace afterhours;

struct ProvidesCamera : BaseComponent {
    IsometricCamera cam;
};

struct Transform : BaseComponent {
    vec2 position{0, 0};
    vec2 velocity{0, 0};
    float radius = 0.2f;

    Transform() = default;
    Transform(vec2 pos) : position(pos) {}
    Transform(float x, float z) : position{x, z} {}
};

enum class FacilityType { Bathroom, Food, Stage };

struct Agent : BaseComponent {
    float time_alive = 0.f;
    int origin_id = -1;
    FacilityType want = FacilityType::Bathroom;
};

struct HasStress : BaseComponent {
    float stress = 0.f;
    float stress_rate = 0.1f;
};

struct Attraction : BaseComponent {
    float spawn_rate = 1.0f;
    float spawn_timer = 0.f;
    int capacity = 50;
    int current_count = 0;
};

struct Facility : BaseComponent {
    FacilityType type = FacilityType::Bathroom;
    float absorption_rate = 2.0f;
    float absorption_timer = 0.f;
    int capacity = 10;
    int current_occupants = 0;
};

struct Artist : BaseComponent {
    float popularity = 0.5f;
    float set_duration = 30.f;
    float set_timer = 0.f;
};

struct PathNode : BaseComponent {
    int next_node_id = -1;
    float width = 1.5f;
};

struct AgentTarget : BaseComponent {
    int facility_id = -1;       // Target Facility entity ID
    vec2 target_pos{0, 0};      // Cached target position
};

struct AgentSteering : BaseComponent {
    vec2 path_direction{0, 0};  // Direction from path following
    vec2 separation{0, 0};      // Separation force from neighbors
};
