
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
constexpr int MAP_SIZE = 50;
constexpr int TILE_RENDER_SIZE = 32;  // pixels at 1x zoom

// Spawn point (left edge, center)
constexpr int SPAWN_X = 0;
constexpr int SPAWN_Z = 25;

// Pre-placed stage position and size
constexpr int STAGE_X = 30;
constexpr int STAGE_Z = 25;
constexpr int STAGE_SIZE = 4;  // 4x4 footprint

// Agent movement speeds (tiles/sec)
constexpr float SPEED_PATH = 0.5f;
constexpr float SPEED_GRASS = 0.25f;

// Spawn rate
constexpr float DEFAULT_SPAWN_INTERVAL = 2.0f;  // seconds between spawns

// Version
constexpr std::string_view VERSION = "0.0.1";

// Check if escape should quit (false if something is consuming it)
bool should_escape_quit();
