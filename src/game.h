
#pragma once

#include "rl.h"
#include "std_include.h"

// Owned by main.cpp
extern bool running;

// Window dimensions
constexpr int DEFAULT_SCREEN_WIDTH = 1280;
constexpr int DEFAULT_SCREEN_HEIGHT = 720;

// Game constants
constexpr float TILESIZE = 1.0f;
constexpr int MAP_SIZE = 52;          // 52x52 total (including fence ring)
constexpr int PLAY_MIN = 1;           // playable area starts at (1,1)
constexpr int PLAY_MAX = 50;          // playable area ends at (50,50)
constexpr int TILE_RENDER_SIZE = 32;  // pixels at 1x zoom

// Gate position (2x1 opening in left fence)
constexpr int GATE_X = 0;
constexpr int GATE_Z1 = 26;
constexpr int GATE_Z2 = 27;

// Spawn point (at the gate)
constexpr int SPAWN_X = 0;
constexpr int SPAWN_Z = 26;

// Pre-placed stage position and size
constexpr int STAGE_X = 30;
constexpr int STAGE_Z = 25;
constexpr int STAGE_SIZE = 4;  // 4x4 footprint

// Agent movement speeds (tiles/sec)
constexpr float SPEED_PATH = 0.5f;
constexpr float SPEED_GRASS = 0.25f;

// Spawn rate
constexpr float DEFAULT_SPAWN_INTERVAL = 2.0f;  // seconds between spawns

// Pre-placed facilities
constexpr int BATHROOM_X = 20;
constexpr int BATHROOM_Z = 20;
constexpr int FOOD_X = 20;
constexpr int FOOD_Z = 30;
constexpr int FACILITY_SIZE = 2;  // 2x2 footprint

// Facility service
constexpr float SERVICE_TIME = 1.0f;     // seconds inside facility
constexpr int FACILITY_MAX_AGENTS = 20;  // density cap before "full"

// Stage watching
constexpr float STAGE_WATCH_RADIUS = 8.0f;  // tiles from stage center

// Version
constexpr std::string_view VERSION = "0.0.1";

// Check if escape should quit (false if something is consuming it)
bool should_escape_quit();
