#pragma once

// AFTER_HOURS_REPLACE_LOGGING and log.h must come first
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/base_component.h"
#include "camera.h"
#include "rl.h"

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

// Hash for FacilityType to use in unordered_map
struct FacilityTypeHash {
    std::size_t operator()(FacilityType t) const {
        return std::hash<int>()(static_cast<int>(t));
    }
};

struct Agent : BaseComponent {
    float time_alive = 0.f;
    int origin_id = -1;
    FacilityType want = FacilityType::Bathroom;

    Agent() = default;
    Agent(FacilityType w, int origin = -1) : origin_id(origin), want(w) {}
};

struct HasStress : BaseComponent {
    float stress = 0.f;
    float stress_rate = 0.1f;

    HasStress() = default;
    HasStress(float s, float rate = 0.1f) : stress(s), stress_rate(rate) {}
};

struct Attraction : BaseComponent {
    float spawn_rate = 1.0f;
    float spawn_timer = 0.f;
    int capacity = 50;
    int current_count = 0;

    Attraction() = default;
    Attraction(float rate, int cap) : spawn_rate(rate), capacity(cap) {}
};

struct Facility : BaseComponent {
    FacilityType type = FacilityType::Bathroom;
    float absorption_rate = 2.0f;
    float absorption_timer = 0.f;
    int capacity = 10;
    int current_occupants = 0;

    Facility() = default;
    Facility(FacilityType t) : type(t) {}
};

struct Artist : BaseComponent {
    float popularity = 0.5f;
    float set_duration = 30.f;
    float set_timer = 0.f;

    Artist() = default;
    Artist(float pop, float duration = 30.f)
        : popularity(pop), set_duration(duration) {}
};

struct PathNode : BaseComponent {
    int next_node_id = -1;
    float width = 1.5f;

    // Congestion tracking
    float capacity = 8.f;      // Max comfortable agents on this segment
    float current_load = 0.f;  // Agent count this frame (reset by system)

    PathNode() = default;
    PathNode(int next, float w = 1.5f) : next_node_id(next), width(w) {}

    float congestion_ratio() const {
        return (capacity > 0.f) ? current_load / capacity : 0.f;
    }
};

// Signpost at each path node - tells agents which way to go for each facility
// type
struct PathSignpost : BaseComponent {
    // Maps FacilityType -> next node ID to reach that facility
    std::unordered_map<FacilityType, int, FacilityTypeHash> next_node_for;

    int get_next_node(FacilityType type) const {
        auto it = next_node_for.find(type);
        return (it != next_node_for.end()) ? it->second : -1;
    }
};

struct AgentTarget : BaseComponent {
    int facility_id = -1;   // Target Facility entity ID
    vec2 target_pos{0, 0};  // Cached target position
};

struct AgentSteering : BaseComponent {
    vec2 path_direction{0, 0};  // Direction from path following
    vec2 separation{0, 0};      // Separation force from neighbors
};

// Marks an agent as being inside a facility
struct InsideFacility : BaseComponent {
    int facility_id = -1;       // Which facility we're inside
    float time_inside = 0.f;    // How long we've been inside
    float service_time = 2.0f;  // How long until we're done
    vec2 slot_offset{0, 0};     // Our position offset within the facility
};

// Stage facility state machine
enum class StageState {
    Idle,        // No artist, no one should go here
    Announcing,  // Artist coming soon, people start heading there
    Performing,  // Artist playing, people can stay/leave
    Clearing     // Performance ended, everyone must leave
};

struct StageInfo : BaseComponent {
    StageState state = StageState::Idle;
    float state_timer = 0.f;

    // Timing configuration
    float idle_duration = 10.f;     // How long between sets
    float announce_duration = 8.f;  // How long the "coming soon" phase lasts
    float perform_duration = 20.f;  // How long the artist performs
    float clear_duration = 5.f;     // How long people have to leave

    // Artist info (for future use)
    float artist_popularity = 0.5f;  // 0-1, affects how many people come

    float get_progress() const {
        switch (state) {
            case StageState::Idle:
                return state_timer / idle_duration;
            case StageState::Announcing:
                return state_timer / announce_duration;
            case StageState::Performing:
                return state_timer / perform_duration;
            case StageState::Clearing:
                return state_timer / clear_duration;
        }
        return 0.f;
    }
};

// Game state tracking - singleton component
enum class GameStatus { Running, GameOver };

struct GameState : BaseComponent {
    GameStatus status = GameStatus::Running;
    float game_time = 0.f;        // Total time played
    float global_stress = 0.f;    // Average stress across all agents
    float max_stress = 0.f;       // Highest individual stress
    int total_agents_served = 0;  // Agents that completed a facility visit
    float game_over_timer = 0.f;  // Time since game over (for animations)

    // Data layer overlay toggle (TAB key) - like Cities: Skylines info views
    bool show_data_layer = false;

    // Lose condition thresholds
    static constexpr float CRITICAL_GLOBAL_STRESS = 0.7f;  // Average stress
    static constexpr float CRITICAL_MAX_STRESS_COUNT =
        10;  // Agents at max stress

    bool is_game_over() const { return status == GameStatus::GameOver; }
};
