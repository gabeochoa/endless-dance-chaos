
#pragma once

#include "rl.h"
#include "std_include.h"

// Window dimensions
constexpr int DEFAULT_SCREEN_WIDTH = 1280;
constexpr int DEFAULT_SCREEN_HEIGHT = 720;

// Game constants
constexpr float TILESIZE = 1.0f;
// Preallocated grid: 102x102. The initial park is the original 52x52
// (fence ring at 0/51, playable 1..50); the rest is Locked tiles until a
// land expansion buys it (see Grid::expansions_x/z). True resize was
// rejected: MAP_SIZE is baked into fixed-size arrays (Grid::tiles,
// event-system scratch arrays, save format), so expansion unlocks a
// region of this larger preallocated grid instead of reallocating it.
// Max: 2 expansions per axis * 25 tiles = 50 tiles per axis, so
// 52 + 50 = 102 exactly fits the fence at the far edge.
constexpr int MAP_SIZE = 102;
constexpr int PLAY_MIN = 1;           // playable area starts at (1,1)
constexpr int PLAY_MAX = 50;          // playable area ends at (50,50) initially
constexpr int TILE_RENDER_SIZE = 32;  // pixels at 1x zoom

// Land expansion: one bought plot adds +25 buildable tiles in +x (East)
// or +z (South), up to 2 plots per axis. Cost doubles per plot bought,
// regardless of axis: 5000, 10000, 20000, 40000.
constexpr int EXPANSION_TILES = 25;
constexpr int EXPANSION_MAX_PER_AXIS = 2;
constexpr int EXPANSION_BASE_COST = 5000;

// Park rating: 0-999, starts at 500. Target = base + coverage +
// inspection memory - harm (death/injury) - need - crowding pressures.
constexpr int PARK_RATING_MIN = 0;
constexpr int PARK_RATING_MAX = 999;
constexpr float PARK_RATING_START = 500.0f;
constexpr float PARK_RATING_BASE = 500.0f;
// Coverage: +40 per facility type (bathroom/food/water) at >=1 facility
// per 100 attendees, scaling linearly below that.
constexpr float PARK_RATING_COVERAGE_PER_TYPE = 40.0f;
constexpr float PARK_RATING_COVERAGE_IDEAL_PER_100 = 1.0f;
// Inspection memory (last grade only): A +60, B +30, C 0, D -50, F -100.
constexpr float PARK_RATING_INSPECTION_A = 60.0f;
constexpr float PARK_RATING_INSPECTION_B = 30.0f;
constexpr float PARK_RATING_INSPECTION_D = -50.0f;
constexpr float PARK_RATING_INSPECTION_F = -100.0f;
// Harm: a death applies its penalty instantly to the rating and to the
// harm pool; an injury adds a smaller amount to the same pool. The pool
// halves at each day rollover (decays over days), so the rating recovers.
constexpr float PARK_RATING_DEATH_PENALTY = 70.0f;
constexpr float PARK_RATING_INJURY_PENALTY = 8.0f;
constexpr float PARK_RATING_HARM_CAP = 400.0f;
constexpr float PARK_RATING_HARM_DECAY_PER_DAY = 0.5f;
// Need / crowding pressures: EWMA toward an instantaneous penalty
// (max 180 / 150) once the unmet-need fraction exceeds the 0.25 baseline
// or agents sit on critically dense tiles. Rating approaches its target
// exponentially at this rate (per second).
constexpr float PARK_RATING_NEED_MAX_PENALTY = 180.0f;
constexpr float PARK_RATING_NEED_BASELINE = 0.25f;
constexpr float PARK_RATING_CROWDING_MAX_PENALTY = 150.0f;
constexpr float PARK_RATING_PRESSURE_RATE = 0.5f;
constexpr float PARK_RATING_APPROACH_RATE = 0.25f;
// Price tolerance: guests tolerate +$10 over the base ticket price at
// rating >= 750. The tolerance ramps linearly from rating 500 to 750
// and stays capped at +10 above that.
constexpr float PARK_RATING_TOLERANCE_START = 500.0f;
constexpr float PARK_RATING_TOLERANCE_FULL = 750.0f;
constexpr float PARK_RATING_TOLERANCE_MAX = 10.0f;
// Spawn multiplier: rating / 500, clamped to [0.5, 1.5]. A 500-rated
// park spawns at the base rate; 999 spawns 1.5x as fast. The multiplier
// is evaluated on the 50-point band floor (575 counts as 550), so
// small rating wobble from a fresh inspection grade does not jitter
// the spawn interval frame to frame; it is exact at band boundaries.
constexpr float PARK_RATING_SPAWN_DIVISOR = 500.0f;
constexpr float PARK_RATING_SPAWN_MIN = 0.5f;
constexpr float PARK_RATING_SPAWN_MAX = 1.5f;
constexpr float PARK_RATING_SPAWN_BAND = 50.0f;

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

// Stage queue: at most this many agents watch at once. Agents who arrive
// after the cap is reached join a FIFO queue (STAGE_QUEUE_MAX slots, the
// south row of StageFloor tiles in front of the stage) instead of
// clumping, and advance as watchers leave. Further excess agents stand
// at their stage spot (overflow) until a queue slot opens.
constexpr int STAGE_WATCH_CAPACITY = 12;
constexpr int STAGE_QUEUE_MAX = 6;

// Scenery: Trees beautify the park. ParkRating grants up to
// PARK_RATING_SCENERY_MAX points, scaled by the fraction of playable
// tiles within PARK_RATING_SCENERY_RADIUS of a Tree.
constexpr float PARK_RATING_SCENERY_MAX = 30.0f;
constexpr float PARK_RATING_SCENERY_RADIUS = 4.0f;

// Scenery build costs (see build_cost_for_tool / build_cost_for_tile).
constexpr int TREE_COST = 50;
constexpr int BENCH_COST = 100;

// Bench rest: agents with rising needs who step onto a Bench sit for
// this long; while seated every need timer grows at BENCH_NEED_MULT.
constexpr float BENCH_REST_SECONDS = 5.0f;
constexpr float BENCH_NEED_MULT = 0.25f;

// Economy (tycoon loop)
constexpr int STARTING_CASH = 10000;
constexpr int DEFAULT_TICKET_PRICE = 20;

// Loans (Finances tab): borrow one of LOAN_AMOUNTS in cash now; the
// principal is tracked as Economy::debt. Interest compounds onto the
// debt once per day rollover at LOAN_INTEREST_RATE_PER_DAY, rounded to
// the nearest dollar (at least $1 while any debt remains). The debt cap
// scales with the festival tier, so a bigger festival can borrow more.
inline constexpr int LOAN_AMOUNTS[] = {5000, 10000, 25000};
constexpr float LOAN_INTEREST_RATE_PER_DAY = 0.005f;  // 0.5%/day on balance
constexpr int LOAN_BASE_DEBT_CAP = 25000;
constexpr int LOAN_DEBT_CAP_PER_TIER = 15000;
constexpr int LOAN_REPAY_STEP = 1000;  // Finances "Repay $1k" button
// Bankruptcy guard: cash floored at 0 (never negative) and the rating
// takes this instant hit (plus the same amount in the harm pool, like a
// death) when cash goes negative after rollover charges. No game-over.
constexpr float CASH_CRISIS_RATING_PENALTY = 50.0f;
// Finance history: the last N closed days of (income, expenses, profit).
constexpr int FINANCE_HISTORY_MAX_DAYS = 7;

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

// Event director: seeded random events, 0-2 per game-day.
// Heat Wave lasts one full game-day at 1x (1440 game-min * 0.5 s/min).
constexpr float EVENT_HEAT_WAVE_DAY_SECONDS = 720.0f;
// Generous Sponsor grant: base * (1 + festival tier).
constexpr int EVENT_SPONSOR_BASE_GRANT = 1000;
// Viral Outbreak: illness spreads to agents within this many tiles
// (Chebyshev) after this many seconds of exposure.
constexpr float EVENT_VIRAL_SPREAD_RADIUS = 2.0f;
constexpr float EVENT_VIRAL_EXPOSURE_SECONDS = 2.0f;
// Equipment Failure: one facility type offline; director rolls 60-120s.
constexpr float EVENT_EQUIPMENT_MIN_SECONDS = 60.0f;
constexpr float EVENT_EQUIPMENT_MAX_SECONDS = 120.0f;

// Guest of Honor / VIP visit: a single VIP guest arrives shortly after a
// VIPVisit event fires, but only at/above this festival tier (tier 2 =
// National, 2000 cumulative arrivals). Below it the event fires with no
// guest — the park is not yet famous enough to draw one.
constexpr int VIP_MIN_TIER = 2;
// Announcement-to-arrival gap: the VIP walks in after the toast, so
// arrival is a timed condition (and an `eventually_` assert target),
// not a same-frame side effect of `trigger_event`.
constexpr float VIP_ARRIVAL_DELAY_SECONDS = 1.0f;
// How long an unserved VIP waits before leaving unhappy. Long enough
// to walk a pathed route from the gate to a facility; short enough
// that ignoring one is a real decision. Per-VIP state, seeded from
// EventDirectorState::vip_patience_seconds (E2E `set_vip_patience`).
constexpr float VIP_PATIENCE_SECONDS = 60.0f;
// First service of a VIP's need: flat bonus, paid once per VIP.
constexpr int VIP_SERVICE_BONUS_CASH = 250;
constexpr float VIP_SERVICE_BONUS_RATING = 25.0f;
// Neglect (collapse, or patience expiry): instant rating hit + the
// same amount added to the harm pool, mirroring death/injury harm.
constexpr float VIP_NEGLECT_RATING_PENALTY = 40.0f;
// Cap on the accumulated VIP bonus pool (it halves each day, like
// the harm pool, so the bonus cannot ratchet the rating up forever).
constexpr float VIP_BONUS_POOL_CAP = 100.0f;

// Version
constexpr std::string_view VERSION = "0.0.1";

// Check if escape should quit (false if something is consuming it)
bool should_escape_quit();
