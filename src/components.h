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

struct Agent : BaseComponent {
    float time_alive = 0.f;
    int origin_id = -1;
};

struct BoidsBehavior : BaseComponent {
    float separation_weight = 1.0f;
    float path_follow_weight = 2.0f;
    float goal_pull_weight = 2.0f;
    float max_speed = 4.0f;
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
    float absorption_rate = 2.0f;
    float absorption_timer = 0.f;
};
