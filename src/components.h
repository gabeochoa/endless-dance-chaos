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

// Signpost at each path tile - tells agents which way to go for each facility
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
    vec2 path_direction{0, 0};    // Direction from path following
    vec2 separation{0, 0};        // Separation force from neighbors
    int wander_target_tile = -1;  // Tile ID we're wandering toward
    float wander_time = 0.f;      // How long we've been wandering (no path)
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

struct PathTile : BaseComponent {
    int grid_x = 0;
    int grid_z = 0;

    // Congestion tracking
    float capacity = 8.f;
    float current_load = 0.f;

    PathTile() = default;
    PathTile(int gx, int gz) : grid_x(gx), grid_z(gz) {}

    float congestion_ratio() const {
        return (capacity > 0.f) ? current_load / capacity : 0.f;
    }
};

// Build tool - what the player is currently placing
enum class BuildTool { Path, Bathroom, Food, Stage };

struct BuilderState : BaseComponent {
    struct PendingTile {
        int grid_x = 0;
        int grid_z = 0;
        bool is_removal = false;  // true = remove existing, false = add new

        PendingTile() = default;
        PendingTile(int gx, int gz, bool remove)
            : grid_x(gx), grid_z(gz), is_removal(remove) {}
    };

    bool active = true;    // Builder mode on/off
    int hover_grid_x = 0;  // Current mouse grid position
    int hover_grid_z = 0;
    bool hover_valid = false;           // Is mouse over valid ground?
    bool path_exists_at_hover = false;  // Already a path here?

    // Current build tool
    BuildTool tool = BuildTool::Path;

    // Staged path changes - not yet committed
    std::vector<PendingTile> pending_tiles;

    // TODO: Future cost tracking
    // int pending_cost = 0;

    bool has_pending() const { return !pending_tiles.empty(); }

    void clear_pending() { pending_tiles.clear(); }

    bool is_pending_at(int gx, int gz) const {
        for (const auto& p : pending_tiles) {
            if (p.grid_x == gx && p.grid_z == gz) return true;
        }
        return false;
    }
};

// Festival progression - tracks unlocked facility slots
struct FestivalProgress : BaseComponent {
    // Available slots per facility type
    int bathroom_slots = 1;
    int food_slots = 1;
    int stage_slots = 1;

    // Currently placed counts
    int bathrooms_placed = 1;
    int food_placed = 1;
    int stages_placed = 1;

    // Milestone tracking
    int last_milestone_agents = 0;

    bool can_place(FacilityType type) const {
        switch (type) {
            case FacilityType::Bathroom:
                return bathrooms_placed < bathroom_slots;
            case FacilityType::Food:
                return food_placed < food_slots;
            case FacilityType::Stage:
                return stages_placed < stage_slots;
        }
        return false;
    }

    int get_slots(FacilityType type) const {
        switch (type) {
            case FacilityType::Bathroom:
                return bathroom_slots;
            case FacilityType::Food:
                return food_slots;
            case FacilityType::Stage:
                return stage_slots;
        }
        return 0;
    }

    int get_placed(FacilityType type) const {
        switch (type) {
            case FacilityType::Bathroom:
                return bathrooms_placed;
            case FacilityType::Food:
                return food_placed;
            case FacilityType::Stage:
                return stages_placed;
        }
        return 0;
    }

    void increment_placed(FacilityType type) {
        switch (type) {
            case FacilityType::Bathroom:
                bathrooms_placed++;
                break;
            case FacilityType::Food:
                food_placed++;
                break;
            case FacilityType::Stage:
                stages_placed++;
                break;
        }
    }

    void decrement_placed(FacilityType type) {
        switch (type) {
            case FacilityType::Bathroom:
                if (bathrooms_placed > 0) bathrooms_placed--;
                break;
            case FacilityType::Food:
                if (food_placed > 0) food_placed--;
                break;
            case FacilityType::Stage:
                if (stages_placed > 0) stages_placed--;
                break;
        }
    }
};

// Agent lingering at stage waiting for next performance
struct LingeringAtStage : BaseComponent {
    int stage_id = -1;
    float linger_time = 0.f;
    float max_linger = 15.f;   // How long before they give up waiting
    vec2 wander_offset{0, 0};  // Random offset for wandering near stage

    LingeringAtStage() = default;
    LingeringAtStage(int sid, float max_time, vec2 wander)
        : stage_id(sid), max_linger(max_time), wander_offset(wander) {}
};

// Demand heatmap data - tracks what agents want at each grid cell
struct DemandHeatmap : BaseComponent {
    // Grid cell -> demand counts [Bathroom, Food, Stage]
    std::map<std::pair<int, int>, std::array<int, 3>> demand_grid;

    // Which demand type to display (0=all, 1=bathroom, 2=food, 3=stage)
    int display_filter = 0;

    void clear() { demand_grid.clear(); }

    void add_demand(int grid_x, int grid_z, FacilityType type) {
        auto& cell = demand_grid[{grid_x, grid_z}];
        cell[static_cast<int>(type)]++;
    }

    int get_demand(int grid_x, int grid_z, FacilityType type) const {
        auto it = demand_grid.find({grid_x, grid_z});
        if (it == demand_grid.end()) return 0;
        return it->second[static_cast<int>(type)];
    }

    int get_total_demand(int grid_x, int grid_z) const {
        auto it = demand_grid.find({grid_x, grid_z});
        if (it == demand_grid.end()) return 0;
        return it->second[0] + it->second[1] + it->second[2];
    }
};
