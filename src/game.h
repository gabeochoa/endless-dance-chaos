
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

// Version
constexpr std::string_view VERSION = "0.0.1";

// Check if escape should quit (false if something is consuming it)
bool should_escape_quit();
