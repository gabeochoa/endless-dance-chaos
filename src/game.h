
#pragma once

#include "rl.h"
#include "std_include.h"

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
constexpr int MEDTENT_X = 15;
constexpr int MEDTENT_Z = 25;
constexpr int FACILITY_SIZE = 2;  // 2x2 footprint

// Facility service
constexpr float SERVICE_TIME = 1.0f;     // seconds inside facility
constexpr int FACILITY_MAX_AGENTS = 20;  // density cap before "full"

// Stage watching
constexpr float STAGE_WATCH_RADIUS = 8.0f;  // tiles from stage center

// Economy (tycoon loop)
constexpr int STARTING_CASH = 10000;
constexpr int DEFAULT_TICKET_PRICE = 20;

// Density & crush damage
constexpr int MAX_AGENTS_PER_TILE = 40;
constexpr float DENSITY_WARNING = 0.50f;    // 20 agents — yellow
constexpr float DENSITY_DANGEROUS = 0.75f;  // 30 agents — movement slows
constexpr float DENSITY_CRITICAL = 0.90f;   // 36 agents — crush damage
constexpr float CRUSH_DAMAGE_RATE = 0.2f;   // HP/sec in critical zone
constexpr int MAX_DEATHS = 10;

// Tycoon growth: Bar / Security Post / Water Station
constexpr int BAR_DRINK_PRICE = 8;  // cash earned per agent served at a Bar
constexpr float BAR_RADIUS = 6.0f;  // injury-risk aura around a Bar
constexpr float BAR_CRUSH_MULT = 1.25f;  // crush damage multiplier near a Bar
constexpr float SECURITY_RADIUS = 6.0f;  // aura around a Security Post
constexpr float SECURITY_CRUSH_MULT = 0.5f;  // crush damage multiplier near one
constexpr float SECURITY_FLEE_MULT = 1.25f;  // flee speed multiplier near one
constexpr float WATER_RADIUS = 6.0f;  // heat-growth relief aura (Water Station)

// Heat / illness need: grows over time, faster in dense crowds, slower near
// water; at max the agent collapses (counts as an injury).
constexpr float COLLAPSE_RESCUE_DELAY = 2.0f;  // sec before medics pick up
constexpr float COLLAPSE_FATAL_TIME = 8.0f;    // sec collapsed before death
constexpr float MEDTENT_RESCUE_RADIUS = 12.0f;

// Staff (RimWorld/RCT): hireable, paid daily wages from Economy.
// Signing bonus deducted on hire; wages deducted at each day rollover.
constexpr int STAFF_GUARD_BONUS = 300;
constexpr int STAFF_GUARD_WAGE = 50;
constexpr int STAFF_MEDIC_BONUS = 400;
constexpr int STAFF_MEDIC_WAGE = 75;
constexpr int STAFF_JANITOR_BONUS = 200;
constexpr int STAFF_JANITOR_WAGE = 30;
constexpr float STAFF_GUARD_RADIUS = 6.0f;        // mobile crush-damage aura
constexpr float STAFF_GUARD_CRUSH_MULT = 0.5f;    // same as a Security Post
constexpr float STAFF_MEDIC_RADIUS = 8.0f;        // rescue aura around a medic
constexpr float STAFF_MEDIC_RESCUE_DELAY = 0.5f;  // vs 2.0s without a medic
constexpr float STAFF_PATROL_RADIUS = 3.0f;  // wander range around home tile
constexpr float STAFF_SPEED = 1.5f;          // tiles/sec (faster than agents)
constexpr float STAFF_JANITOR_SERVICE_MULT = 0.1f;  // -10% service time each

// Inspection: one per game-day, scored from the previous day's record.
constexpr int INSPECTION_GRANT_A = 500;
constexpr int INSPECTION_GRANT_B = 250;

// Version
constexpr std::string_view VERSION = "0.0.1";

// Check if escape should quit (false if something is consuming it)
bool should_escape_quit();
