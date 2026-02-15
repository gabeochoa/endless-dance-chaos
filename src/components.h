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
enum class TileType { Grass, Path, Fence, Gate, Stage, Bathroom, Food };

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
};

// Facility types (for agents to want)
enum class FacilityType { Bathroom, Food, Stage };

// Minimal agent component - will be fleshed out in later phases
struct Agent : afterhours::BaseComponent {
    FacilityType want = FacilityType::Bathroom;

    Agent() = default;
    Agent(FacilityType w) : want(w) {}
};

// Game state tracking - singleton component
enum class GameStatus { Running, GameOver };

struct GameState : afterhours::BaseComponent {
    GameStatus status = GameStatus::Running;
    float game_time = 0.f;
    bool show_data_layer = false;

    bool is_game_over() const { return status == GameStatus::GameOver; }
};

// Build tool - what the player is currently placing
enum class BuildTool { Path, Bathroom, Food, Stage };

struct BuilderState : afterhours::BaseComponent {
    struct PendingTile {
        int grid_x = 0;
        int grid_z = 0;
        bool is_removal = false;

        PendingTile() = default;
        PendingTile(int gx, int gz, bool remove)
            : grid_x(gx), grid_z(gz), is_removal(remove) {}
    };

    bool active = true;
    int hover_grid_x = 0;
    int hover_grid_z = 0;
    bool hover_valid = false;
    bool path_exists_at_hover = false;

    BuildTool tool = BuildTool::Path;

    std::vector<PendingTile> pending_tiles;

    bool has_pending() const { return !pending_tiles.empty(); }

    void clear_pending() { pending_tiles.clear(); }

    bool is_pending_at(int gx, int gz) const {
        for (const auto& p : pending_tiles) {
            if (p.grid_x == gx && p.grid_z == gz) return true;
        }
        return false;
    }
};
