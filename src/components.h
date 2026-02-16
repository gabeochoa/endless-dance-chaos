#pragma once

// AFTER_HOURS_REPLACE_LOGGING and log.h must come first
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/base_component.h"
#include "camera.h"
#include "game.h"
#include "rl.h"

struct ProvidesCamera : afterhours::BaseComponent {
    IsometricCamera cam;
};

struct Transform : afterhours::BaseComponent {
    ::vec2 position{0, 0};
    ::vec2 velocity{0, 0};
    float radius = 0.2f;

    Transform() = default;
    Transform(::vec2 pos) : position(pos) {}
    Transform(float x, float z) : position{x, z} {}
};

// Tile types for the grid
enum class TileType {
    Grass,
    Path,
    Fence,
    Gate,
    Stage,
    StageFloor,
    Bathroom,
    Food,
    MedTent
};

// Single tile in the grid
struct Tile {
    TileType type = TileType::Grass;
    int agent_count = 0;

    // 5 pheromone channels: Bathroom, Food, Stage, Exit, MedTent
    std::array<uint8_t, 5> pheromone = {0, 0, 0, 0, 0};

    static constexpr int PHERO_BATHROOM = 0;
    static constexpr int PHERO_FOOD = 1;
    static constexpr int PHERO_STAGE = 2;
    static constexpr int PHERO_EXIT = 3;
    static constexpr int PHERO_MEDTENT = 4;

    static float to_strength(uint8_t val) { return val * (10.0f / 255.0f); }
    static uint8_t from_strength(float s) {
        return static_cast<uint8_t>(std::clamp(s * 25.5f, 0.0f, 255.0f));
    }
};

// Grid singleton - holds the MAP_SIZE x MAP_SIZE tile map
struct Grid : afterhours::BaseComponent {
    std::array<Tile, MAP_SIZE * MAP_SIZE> tiles{};

    // Cached gate positions for fast access during exodus / count_gates
    std::vector<std::pair<int, int>> gate_positions;

    int index(int x, int z) const { return z * MAP_SIZE + x; }

    bool in_bounds(int x, int z) const {
        return x >= 0 && x < MAP_SIZE && z >= 0 && z < MAP_SIZE;
    }

    Tile& at(int x, int z) { return tiles[index(x, z)]; }

    const Tile& at(int x, int z) const { return tiles[index(x, z)]; }

    // Convert world position to grid coordinates
    std::pair<int, int> world_to_grid(float wx, float wz) const {
        return {(int) std::floor(wx / TILESIZE + 0.5f),
                (int) std::floor(wz / TILESIZE + 0.5f)};
    }

    // Convert grid coordinates to world position (center of tile)
    ::vec2 grid_to_world(int x, int z) const {
        return {x * TILESIZE, z * TILESIZE};
    }

    // Check if position is in the playable area (inside fence)
    bool in_playable(int x, int z) const {
        return x >= PLAY_MIN && x <= PLAY_MAX && z >= PLAY_MIN && z <= PLAY_MAX;
    }

    // Fill a rectangular footprint with the given tile type
    void place_footprint(int x, int z, int w, int h, TileType type) {
        for (int dz = 0; dz < h; dz++)
            for (int dx = 0; dx < w; dx++)
                if (in_bounds(x + dx, z + dz)) at(x + dx, z + dz).type = type;
    }

    // Rebuild the gate position cache (call after placing/removing gates)
    void rebuild_gate_cache() {
        gate_positions.clear();
        for (int z = 0; z < MAP_SIZE; z++)
            for (int x = 0; x < MAP_SIZE; x++)
                if (at(x, z).type == TileType::Gate)
                    gate_positions.push_back({x, z});
    }

    // Number of gate pairs (each gate is 2 tiles)
    int gate_count() const {
        return static_cast<int>(gate_positions.size()) / 2;
    }

    // Initialize perimeter fence, gate, and pre-placed facilities
    void init_perimeter() {
        // Perimeter fence: outer ring
        for (int i = 0; i < MAP_SIZE; i++) {
            at(i, 0).type = TileType::Fence;             // top row
            at(i, MAP_SIZE - 1).type = TileType::Fence;  // bottom row
            at(0, i).type = TileType::Fence;             // left col
            at(MAP_SIZE - 1, i).type = TileType::Fence;  // right col
        }

        // Gate (2x1 opening in left fence)
        at(GATE_X, GATE_Z1).type = TileType::Gate;
        at(GATE_X, GATE_Z2).type = TileType::Gate;

        // Stage floor (watch zone) — circular area around stage center
        float scx = STAGE_X + STAGE_SIZE / 2.0f;
        float scz = STAGE_Z + STAGE_SIZE / 2.0f;
        for (int z = 0; z < MAP_SIZE; z++) {
            for (int x = 0; x < MAP_SIZE; x++) {
                float dx = x - scx;
                float dz = z - scz;
                float dist = std::sqrt(dx * dx + dz * dz);
                if (dist <= STAGE_WATCH_RADIUS &&
                    at(x, z).type == TileType::Grass) {
                    at(x, z).type = TileType::StageFloor;
                }
            }
        }

        // Stage (4x4) — placed after floor so it overwrites the center
        place_footprint(STAGE_X, STAGE_Z, STAGE_SIZE, STAGE_SIZE,
                        TileType::Stage);
        // Bathroom (2x2)
        place_footprint(BATHROOM_X, BATHROOM_Z, FACILITY_SIZE, FACILITY_SIZE,
                        TileType::Bathroom);
        // Food (2x2)
        place_footprint(FOOD_X, FOOD_Z, FACILITY_SIZE, FACILITY_SIZE,
                        TileType::Food);
        // Medical Tent (2x2)
        place_footprint(MEDTENT_X, MEDTENT_Z, FACILITY_SIZE, FACILITY_SIZE,
                        TileType::MedTent);

        rebuild_gate_cache();
    }
};

// Facility types (for agents to want)
enum class FacilityType { Bathroom, Food, Stage, Exit, MedTent };

// Map FacilityType to its corresponding TileType.
// Indexed by FacilityType enum value.
inline TileType facility_type_to_tile(FacilityType type) {
    constexpr TileType MAP[] = {
        TileType::Bathroom,  // Bathroom
        TileType::Food,      // Food
        TileType::Stage,     // Stage
        TileType::Gate,      // Exit
        TileType::MedTent,   // MedTent
    };
    return MAP[static_cast<int>(type)];
}

// Agent component - walks toward target using greedy neighbor pathfinding
struct Agent : afterhours::BaseComponent {
    FacilityType want = FacilityType::Stage;
    int target_grid_x = -1;
    int target_grid_z = -1;
    float speed = SPEED_PATH;

    int flee_target_x = -1;
    int flee_target_z = -1;

    // Visual variety: color palette index (0-7)
    uint8_t color_idx = 0;

    Agent() = default;
    Agent(FacilityType w) : want(w) {}
    Agent(FacilityType w, int tx, int tz)
        : want(w), target_grid_x(tx), target_grid_z(tz) {}
};

// Agent need timers - triggers bathroom/food seeking behavior
struct AgentNeeds : afterhours::BaseComponent {
    float bathroom_timer = 0.f;
    float bathroom_threshold = 0.f;  // random 30-90 sec
    float food_timer = 0.f;
    float food_threshold = 0.f;  // random 45-120 sec
    bool needs_bathroom = false;
    bool needs_food = false;
};

// Attached while agent is watching the stage (standing in front zone)
struct WatchingStage : afterhours::BaseComponent {
    float watch_timer = 0.f;
    float watch_duration = 0.f;  // random 30-120 sec
};

// Agent health - takes crush damage at critical density
struct AgentHealth : afterhours::BaseComponent {
    float hp = 1.0f;
};

// Visual particle for death bursts
struct Particle : afterhours::BaseComponent {
    vec2 velocity;
    float lifetime = 0.f;
    float max_lifetime = 0.f;
    float size = 3.f;
    raylib::Color color = {255, 80, 80, 255};
};

// Pheromone depositor: agent leaves trail after exiting facility
struct PheromoneDepositor : afterhours::BaseComponent {
    FacilityType leaving_type = FacilityType::Bathroom;
    bool is_depositing = false;
    float deposit_distance = 0.f;
    static constexpr float MAX_DEPOSIT_DISTANCE = 30.0f;
};

// Tag: agent carried over from previous day (stuck after exodus)
struct CarryoverAgent : afterhours::BaseComponent {};

// Attached while agent is being serviced inside a facility
struct BeingServiced : afterhours::BaseComponent {
    int facility_grid_x = 0;
    int facility_grid_z = 0;
    FacilityType facility_type = FacilityType::Bathroom;
    float time_remaining = SERVICE_TIME;
};

// Game state tracking - singleton component
enum class GameStatus { Running, GameOver };

struct GameState : afterhours::BaseComponent {
    GameStatus status = GameStatus::Running;
    float game_time = 0.f;
    bool show_data_layer = false;
    int death_count = 0;
    int max_deaths = MAX_DEATHS;
    float speed_multiplier = 1.0f;
    bool show_debug = false;
    int total_agents_served = 0;
    float time_survived = 0.f;  // total seconds played
    int max_attendees = 0;      // peak simultaneous agents
    int agents_exited = 0;      // agents who left through gates
    int carryover_count = 0;    // agents stuck after exodus

    bool is_game_over() const { return status == GameStatus::GameOver; }
};

// Toast notification (fades after a few seconds)
struct ToastMessage : afterhours::BaseComponent {
    std::string text;
    float lifetime = 3.0f;
    float elapsed = 0.f;
    float fade_duration = 0.5f;
};

// Facility slot tracking for progression
struct FacilitySlots : afterhours::BaseComponent {
    int stages_placed = 1;
    int bathrooms_placed = 1;
    int food_placed = 1;
    int gates_placed = 1;

    int get_slots_per_type(int max_attendees_ever) const {
        return 1 + (max_attendees_ever / 100);
    }

    bool can_place(FacilityType type, int max_attendees_ever) const {
        int slots = get_slots_per_type(max_attendees_ever);
        switch (type) {
            case FacilityType::Bathroom:
                return bathrooms_placed < slots;
            case FacilityType::Food:
                return food_placed < slots;
            case FacilityType::Stage:
                return stages_placed < slots;
            default:
                return true;
        }
    }
};

// Build tool - what the player is currently placing
enum class BuildTool {
    Path,
    Fence,
    Gate,
    Stage,
    Bathroom,
    Food,
    MedTent,
    Demolish
};

// Random event types that affect gameplay
enum class EventType { Rain, PowerOutage, VIPVisit, HeatWave };

// Active random event
struct ActiveEvent : afterhours::BaseComponent {
    EventType type = EventType::Rain;
    float duration = 0.f;  // total seconds
    float elapsed = 0.f;   // seconds elapsed
    std::string description;
    bool notified = false;
};

// Difficulty scaling singleton
struct DifficultyState : afterhours::BaseComponent {
    int day_number = 1;
    float spawn_rate_mult = 1.0f;
    float crowd_size_mult = 1.0f;
    float event_timer = 0.f;
    float next_event_time = 120.f;  // first event after 2 minutes
};

struct BuilderState : afterhours::BaseComponent {
    bool active = true;
    BuildTool tool = BuildTool::Path;
};

// Game clock - 24-hour cycle with phases
enum class GameSpeed { Paused, OneX, TwoX, FourX };

struct GameClock : afterhours::BaseComponent {
    float game_time_minutes = 600.0f;  // Start at 10:00am
    GameSpeed speed = GameSpeed::OneX;
    float debug_time_mult = 0.f;  // >0 = debug override active

    // 12 real minutes = 24 game hours = 1440 game minutes
    static constexpr float SECONDS_PER_GAME_MINUTE = 0.5f;

    float speed_multiplier() const {
        if (debug_time_mult > 0.f) return debug_time_mult;
        switch (speed) {
            case GameSpeed::Paused:
                return 0.0f;
            case GameSpeed::OneX:
                return 1.0f;
            case GameSpeed::TwoX:
                return 2.0f;
            case GameSpeed::FourX:
                return 4.0f;
        }
        return 1.0f;
    }

    enum class Phase { Day, Night, Exodus, DeadHours };

    Phase get_phase() const {
        int hour = get_hour();
        if (hour >= 10 && hour < 18) return Phase::Day;
        if (hour >= 18) return Phase::Night;
        if (hour < 3) return Phase::Exodus;
        return Phase::DeadHours;  // 3am - 10am
    }

    int get_hour() const {
        return static_cast<int>(game_time_minutes / 60.0f) % 24;
    }
    int get_minute() const { return static_cast<int>(game_time_minutes) % 60; }

    std::string format_time() const {
        return fmt::format("{:02d}:{:02d}", get_hour(), get_minute());
    }

    static const char* phase_name(Phase p) {
        switch (p) {
            case Phase::Day:
                return "Day";
            case Phase::Night:
                return "Night";
            case Phase::Exodus:
                return "Exodus";
            case Phase::DeadHours:
                return "Dead Hours";
        }
        return "Unknown";
    }
};

// Stage state machine
enum class StageState { Idle, Announcing, Performing, Clearing };

// Single scheduled artist
struct ScheduledArtist {
    std::string name;
    float start_time_minutes = 0.f;
    float duration_minutes = 60.f;
    int expected_crowd = 100;
    bool announced = false;
    bool performing = false;
    bool finished = false;
};

// Artist schedule singleton (sliding window)
struct ArtistSchedule : afterhours::BaseComponent {
    std::vector<ScheduledArtist> schedule;
    int look_ahead = 6;
    StageState stage_state = StageState::Idle;
    int current_artist_idx = -1;

    ScheduledArtist* get_current() {
        if (current_artist_idx >= 0 &&
            current_artist_idx < (int) schedule.size() &&
            schedule[current_artist_idx].performing)
            return &schedule[current_artist_idx];
        return nullptr;
    }

    ScheduledArtist* get_next() {
        for (auto& a : schedule) {
            if (!a.finished && !a.performing) return &a;
        }
        return nullptr;
    }
};

// Spawn control singleton
struct SpawnState : afterhours::BaseComponent {
    float interval = DEFAULT_SPAWN_INTERVAL;  // seconds between spawns
    float timer = 0.f;
    bool enabled = true;
    bool manual_override = false;  // debug slider active, skip auto-adjust
};

// Path drawing state - rectangle drag on grid
struct PathDrawState : afterhours::BaseComponent {
    // Current hover position (updated every frame)
    int hover_x = 0;
    int hover_z = 0;
    bool hover_valid = false;

    // Rectangle drawing
    bool is_drawing = false;
    int start_x = 0;
    int start_z = 0;

    // Demolish mode
    bool demolish_mode = false;

    // When > 0, HoverTrackingSystem skips and decrements instead of
    // overwriting hover. Used by E2E click_grid to preserve injected hover.
    int hover_lock_frames = 0;

    // Get rectangle bounds (min/max) for drawing preview
    void get_rect(int& min_x, int& min_z, int& max_x, int& max_z) const {
        min_x = std::min(start_x, hover_x);
        min_z = std::min(start_z, hover_z);
        max_x = std::max(start_x, hover_x);
        max_z = std::max(start_z, hover_z);
    }
};
