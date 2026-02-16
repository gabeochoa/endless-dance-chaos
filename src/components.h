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
    Food
};

// Single tile in the grid
struct Tile {
    TileType type = TileType::Grass;
    int agent_count = 0;
};

// Grid singleton - holds the MAP_SIZE x MAP_SIZE tile map
struct Grid : afterhours::BaseComponent {
    std::array<Tile, MAP_SIZE * MAP_SIZE> tiles{};

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
        for (int z = STAGE_Z; z < STAGE_Z + STAGE_SIZE; z++)
            for (int x = STAGE_X; x < STAGE_X + STAGE_SIZE; x++)
                if (in_bounds(x, z)) at(x, z).type = TileType::Stage;

        // Bathroom (2x2)
        for (int z = BATHROOM_Z; z < BATHROOM_Z + FACILITY_SIZE; z++)
            for (int x = BATHROOM_X; x < BATHROOM_X + FACILITY_SIZE; x++)
                if (in_bounds(x, z)) at(x, z).type = TileType::Bathroom;

        // Food (2x2)
        for (int z = FOOD_Z; z < FOOD_Z + FACILITY_SIZE; z++)
            for (int x = FOOD_X; x < FOOD_X + FACILITY_SIZE; x++)
                if (in_bounds(x, z)) at(x, z).type = TileType::Food;
    }
};

// Facility types (for agents to want)
enum class FacilityType { Bathroom, Food, Stage };

// Agent component - walks toward target using greedy neighbor pathfinding
struct Agent : afterhours::BaseComponent {
    FacilityType want = FacilityType::Stage;
    int target_grid_x = -1;
    int target_grid_z = -1;
    float speed = SPEED_PATH;  // current speed, adjusted by terrain

    // Flee state: committed direction when escaping dangerous density
    int flee_target_x = -1;
    int flee_target_z = -1;

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

    bool is_game_over() const { return status == GameStatus::GameOver; }
};

// Build tool - what the player is currently placing
enum class BuildTool { Path, Bathroom, Food, Stage };

struct BuilderState : afterhours::BaseComponent {
    bool active = true;
    BuildTool tool = BuildTool::Path;
};

// Game clock - 24-hour cycle with phases
enum class GameSpeed { Paused, OneX, TwoX, FourX };

struct GameClock : afterhours::BaseComponent {
    float game_time_minutes = 600.0f;  // Start at 10:00am
    GameSpeed speed = GameSpeed::OneX;

    // 12 real minutes = 24 game hours = 1440 game minutes
    static constexpr float SECONDS_PER_GAME_MINUTE = 0.5f;

    float speed_multiplier() const {
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

// Spawn control singleton
struct SpawnState : afterhours::BaseComponent {
    float interval = DEFAULT_SPAWN_INTERVAL;  // seconds between spawns
    float timer = 0.f;
    bool enabled = true;
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

    // Get rectangle bounds (min/max) for drawing preview
    void get_rect(int& min_x, int& min_z, int& max_x, int& max_z) const {
        min_x = std::min(start_x, hover_x);
        min_z = std::min(start_z, hover_z);
        max_x = std::max(start_x, hover_x);
        max_z = std::max(start_z, hover_z);
    }
};
