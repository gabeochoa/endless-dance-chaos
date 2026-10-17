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
// New types append at the end: TILE_BLOCKS / TILE_DAY_COLORS /
// TILE_NIGHT_COLORS are indexed by TileType, so they must grow in sync.
enum class TileType {
    Grass,
    Path,
    Fence,
    Gate,
    Stage,
    StageFloor,
    Bathroom,
    Food,
    MedTent,
    Bar,
    SecurityPost,
    WaterStation
};

// Single tile in the grid
struct Tile {
    TileType type = TileType::Grass;
    int agent_count = 0;

    // Per-desire agent counts (indexed by FacilityType enum).
    // Updated each frame by UpdateTileDensitySystem alongside agent_count.
    static constexpr int NUM_DESIRES = 5;
    std::array<int, NUM_DESIRES> desire_counts = {0, 0, 0, 0, 0};

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

    // Cached facility tile positions (individual tiles, not anchors).
    // Used by find_nearest_facility to avoid full grid scans.
    std::vector<std::pair<int, int>> bathroom_positions;
    std::vector<std::pair<int, int>> food_positions;
    std::vector<std::pair<int, int>> medtent_positions;
    std::vector<std::pair<int, int>> bar_positions;
    std::vector<std::pair<int, int>> security_positions;
    std::vector<std::pair<int, int>> water_positions;

    // Cached StageFloor positions sorted by distance from stage center.
    // Used by best_stage_spot to find closest non-crowded tile to an agent.
    struct StageSpot {
        int x, z;
        float dist;
    };
    std::vector<StageSpot> stage_floor_spots;

    // Cached facility label info for RenderFacilityLabelsSystem.
    struct FacilityLabel {
        const char* text;
        float world_x, world_z;
        uint8_t r, g, b;
    };
    std::vector<FacilityLabel> facility_labels;

    // Dirty flags for lazy cache rebuilds
    bool caches_dirty = true;
    bool minimap_dirty = true;

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

    // Mark tile caches as needing rebuild (call after any tile type change)
    void mark_tiles_dirty() {
        caches_dirty = true;
        minimap_dirty = true;
    }

    // Fill a rectangular footprint with the given tile type
    void place_footprint(int x, int z, int w, int h, TileType type) {
        for (int dz = 0; dz < h; dz++)
            for (int dx = 0; dx < w; dx++)
                if (in_bounds(x + dx, z + dz)) at(x + dx, z + dz).type = type;
        mark_tiles_dirty();
    }

    // Rebuild all tile-derived caches if dirty
    void ensure_caches() {
        if (!caches_dirty) return;
        caches_dirty = false;

        // Gate positions
        gate_positions.clear();
        bathroom_positions.clear();
        food_positions.clear();
        medtent_positions.clear();
        bar_positions.clear();
        security_positions.clear();
        water_positions.clear();
        for (int z = 0; z < MAP_SIZE; z++) {
            for (int x = 0; x < MAP_SIZE; x++) {
                switch (at(x, z).type) {
                    case TileType::Gate:
                        gate_positions.push_back({x, z});
                        break;
                    case TileType::Bathroom:
                        bathroom_positions.push_back({x, z});
                        break;
                    case TileType::Food:
                        food_positions.push_back({x, z});
                        break;
                    case TileType::MedTent:
                        medtent_positions.push_back({x, z});
                        break;
                    case TileType::Bar:
                        bar_positions.push_back({x, z});
                        break;
                    case TileType::SecurityPost:
                        security_positions.push_back({x, z});
                        break;
                    case TileType::WaterStation:
                        water_positions.push_back({x, z});
                        break;
                    default:
                        break;
                }
            }
        }

        // StageFloor spots sorted by distance from stage center
        float scx = STAGE_X + STAGE_SIZE / 2.0f;
        float scz = STAGE_Z + STAGE_SIZE / 2.0f;
        stage_floor_spots.clear();
        int r = static_cast<int>(std::ceil(STAGE_WATCH_RADIUS));
        int cx = static_cast<int>(scx);
        int cz = static_cast<int>(scz);
        for (int z = cz - r; z <= cz + r; z++) {
            for (int x = cx - r; x <= cx + r; x++) {
                if (!in_bounds(x, z)) continue;
                if (at(x, z).type != TileType::StageFloor) continue;
                float dx = x - scx;
                float dz = z - scz;
                stage_floor_spots.push_back(
                    {x, z, std::sqrt(dx * dx + dz * dz)});
            }
        }
        std::sort(stage_floor_spots.begin(), stage_floor_spots.end(),
                  [](const StageSpot& a, const StageSpot& b) {
                      return a.dist < b.dist;
                  });

        // Facility labels for the render system
        rebuild_facility_labels();
    }

    // Number of gate pairs (each gate is 2 tiles)
    int gate_count() {
        ensure_caches();
        return static_cast<int>(gate_positions.size()) / 2;
    }

    // Legacy compatibility: explicit gate cache rebuild
    void rebuild_gate_cache() { mark_tiles_dirty(); }

    // Get cached positions for a facility TileType
    const std::vector<std::pair<int, int>>& get_facility_positions(
        TileType type) {
        ensure_caches();
        switch (type) {
            case TileType::Bathroom:
                return bathroom_positions;
            case TileType::Food:
                return food_positions;
            case TileType::MedTent:
                return medtent_positions;
            case TileType::Bar:
                return bar_positions;
            case TileType::SecurityPost:
                return security_positions;
            case TileType::WaterStation:
                return water_positions;
            case TileType::Gate:
                return gate_positions;
            default:
                // Return empty for non-facility types
                static const std::vector<std::pair<int, int>> empty;
                return empty;
        }
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

        mark_tiles_dirty();
    }

   private:
    // Rebuild facility label positions for the render system
    void rebuild_facility_labels() {
        facility_labels.clear();

        // Stage (always at fixed position)
        facility_labels.push_back(
            {"STAGE", (STAGE_X + STAGE_SIZE / 2.0f) * TILESIZE,
             (STAGE_Z + STAGE_SIZE / 2.0f) * TILESIZE, 255, 217, 61});

        // Scan for unique facility anchors (top-left of each 2x2)
        bool seen[MAP_SIZE * MAP_SIZE] = {};
        auto add_facility = [&](int x, int z, TileType /*type*/,
                                const char* text, uint8_t r, uint8_t g,
                                uint8_t b, int w = 2, int h = 2) {
            int idx = z * MAP_SIZE + x;
            if (seen[idx]) return;
            facility_labels.push_back({text, (x + w / 2.0f) * TILESIZE,
                                       (z + h / 2.0f) * TILESIZE, r, g, b});
            for (int dz = 0; dz < h; dz++)
                for (int dx = 0; dx < w; dx++)
                    if (in_bounds(x + dx, z + dz))
                        seen[(z + dz) * MAP_SIZE + x + dx] = true;
        };

        for (int z = 0; z < MAP_SIZE; z++) {
            for (int x = 0; x < MAP_SIZE; x++) {
                TileType t = at(x, z).type;
                if (t == TileType::Bathroom)
                    add_facility(x, z, t, "WC", 126, 207, 192);
                else if (t == TileType::Food)
                    add_facility(x, z, t, "FOOD", 244, 164, 164);
                else if (t == TileType::MedTent)
                    add_facility(x, z, t, "MED", 255, 100, 100);
                else if (t == TileType::Bar)
                    add_facility(x, z, t, "BAR", 255, 160, 40);
                else if (t == TileType::SecurityPost)
                    add_facility(x, z, t, "SEC", 140, 160, 255, 1, 1);
                else if (t == TileType::WaterStation)
                    add_facility(x, z, t, "WATER", 80, 180, 255, 1, 1);
            }
        }

        // First gate
        if (!gate_positions.empty()) {
            auto [gx, gz] = gate_positions[0];
            facility_labels.push_back(
                {"GATE", gx * TILESIZE, (gz + 0.5f) * TILESIZE, 68, 136, 170});
        }
    }
};

// Facility types (for agents to want)
// Bar / WaterStation append at the end. They have no pheromone channel
// (facility_to_channel returns -1 for them) and no desire-count slot:
// Tile::NUM_DESIRES stays 5 and crowd code guards the index.
enum class FacilityType {
    Bathroom,
    Food,
    Stage,
    Exit,
    MedTent,
    Bar,
    WaterStation
};

// Map FacilityType to its corresponding TileType.
// Indexed by FacilityType enum value.
inline TileType facility_type_to_tile(FacilityType type) {
    constexpr TileType MAP[] = {
        TileType::Bathroom,      // Bathroom
        TileType::Food,          // Food
        TileType::Stage,         // Stage
        TileType::Gate,          // Exit
        TileType::MedTent,       // MedTent
        TileType::Bar,           // Bar
        TileType::WaterStation,  // WaterStation
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

    // Cached pathfinding result: the next tile the agent is walking toward.
    // Only recomputed on tile transitions or goal changes.
    int move_target_x = -1;
    int move_target_z = -1;

    // Stuck detection: tracks time without progress toward target.
    // When stuck_timer exceeds STUCK_FORCE_THRESHOLD, the agent forces
    // through crowded tiles (ignoring slowdown and flee) unless lethal.
    float stuck_timer = 0.f;
    int last_grid_x = -1;
    int last_grid_z = -1;
    static constexpr float STUCK_FORCE_THRESHOLD = 3.0f;  // seconds

    bool is_forcing() const { return stuck_timer >= STUCK_FORCE_THRESHOLD; }

    void set_target(int x, int z) {
        target_grid_x = x;
        target_grid_z = z;
        move_target_x = -1;
        move_target_z = -1;
    }

    Agent() = default;
    Agent(FacilityType w) : want(w) {}
    Agent(FacilityType w, int tx, int tz)
        : want(w), target_grid_x(tx), target_grid_z(tz) {}
};

// Agent need timers - triggers bathroom/food/heat/bar seeking behavior
struct AgentNeeds : afterhours::BaseComponent {
    float bathroom_timer = 0.f;
    float bathroom_threshold = 0.f;  // random 30-90 sec
    float food_timer = 0.f;
    float food_threshold = 0.f;  // random 45-120 sec
    float heat_timer = 0.f;
    float heat_threshold = 0.f;  // random 90-180 sec; max => collapse
    float bar_timer = 0.f;
    float bar_threshold = 0.f;  // random 60-180 sec
    bool needs_bathroom = false;
    bool needs_food = false;
    bool needs_heat = false;
    bool needs_bar = false;
    float fun_boost =
        0.f;  // >0 after Bar service: next stage watch lasts longer
};

// Attached while an agent has collapsed from heat: immobile until medics
// rescue them (nearby MedTent) or COLLAPSE_FATAL_TIME elapses (death).
struct Collapsed : afterhours::BaseComponent {
    float timer = 0.f;
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
    Color color = {255, 80, 80, 255};
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
    int injury_count = 0;  // heat collapses; tracked separately from deaths
    int max_deaths = MAX_DEATHS;
    int total_arrivals = 0;  // agents who paid at a gate (milestone progress)
    int milestone_idx = 0;   // index into the milestone threshold table
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
// New tools append after Demolish so every existing tool keeps its index
// (build-bar slots, tool-cycling tests, and key bindings depend on it).
enum class BuildTool {
    Path,
    Fence,
    Gate,
    Stage,
    Bathroom,
    Food,
    MedTent,
    Demolish,
    Bar,
    SecurityPost,
    WaterStation
};

// Economy singleton: tycoon money flows (cash in from tickets, cash out
// for building). One instance lives on the Sophie entity.
struct Economy : afterhours::BaseComponent {
    int cash = STARTING_CASH;
    int ticket_price = DEFAULT_TICKET_PRICE;
    int total_earned = 0;
    int total_spent = 0;

    static constexpr int MIN_TICKET_PRICE = 5;
    static constexpr int MAX_TICKET_PRICE = 100;
    static constexpr int TICKET_PRICE_STEP = 5;

    // Clamped +/- adjustment shared by the price keys, the HUD buttons,
    // and the E2E ticket_price_up/down commands.
    void adjust_price(int delta) {
        ticket_price = std::clamp(ticket_price + delta, MIN_TICKET_PRICE,
                                  MAX_TICKET_PRICE);
    }

    // Spawn-rate factor: higher price => fewer arrivals. Multiplied into
    // the spawn interval by the schedule systems.
    float price_spawn_factor() const {
        return ticket_price / static_cast<float>(DEFAULT_TICKET_PRICE);
    }

    void earn(int amount) {
        cash += amount;
        total_earned += amount;
    }

    // Deducts amount if affordable; returns false and changes nothing when
    // cash is insufficient.
    bool try_spend(int amount) {
        if (amount < 0 || cash < amount) return false;
        cash -= amount;
        total_spent += amount;
        return true;
    }
};

// Build costs. Path/Fence/Demolish are free; facilities cost money.
// No pre-existing cost table existed, so this is the single table both the
// real build path (PathBuildSystem) and the E2E place_building helper use.
inline int build_cost_for_tool(BuildTool tool) {
    switch (tool) {
        case BuildTool::Gate:
            return 250;
        case BuildTool::Stage:
            return 2000;
        case BuildTool::Bathroom:
            return 500;
        case BuildTool::Food:
            return 500;
        case BuildTool::MedTent:
            return 750;
        case BuildTool::Bar:
            return 600;
        case BuildTool::SecurityPost:
            return 400;
        case BuildTool::WaterStation:
            return 200;
        default:
            return 0;
    }
}

inline int build_cost_for_tile(TileType type) {
    switch (type) {
        case TileType::Gate:
            return 250;
        case TileType::Stage:
            return 2000;
        case TileType::Bathroom:
            return 500;
        case TileType::Food:
            return 500;
        case TileType::MedTent:
            return 750;
        case TileType::Bar:
            return 600;
        case TileType::SecurityPost:
            return 400;
        case TileType::WaterStation:
            return 200;
        default:
            return 0;
    }
}

// Inspection state singleton: one health-and-safety inspection per game-day.
// Grades A-F; F, or 3 consecutive D/F grades, shuts the festival down.
struct InspectionState : afterhours::BaseComponent {
    char last_grade = '-';  // '-' = no inspection yet
    int last_score = -1;
    int inspections_done = 0;
    int consecutive_bad = 0;     // consecutive D/F grades
    int density_violations = 0;  // seconds with a critically dense tile
    int deaths_at_last = 0;      // death_count snapshot at last inspection
    int injuries_at_last = 0;    // injury_count snapshot at last inspection
    int last_inspected_day = 1;
    bool shutdown_triggered = false;
    bool force_inspection = false;  // E2E run_inspection: inspect next frame
};

// ── Lineup booking + festival tiers ──────────────────────────────────────
// Bookable acts: the data table for the sidebar booking panel. `draw_mult`
// multiplies next-day arrivals (spawn rate), `fun_mult` multiplies
// stage-watch duration while the act is playing. `min_tier` gates the act
// in the booking panel until the festival reaches that tier.
// `short_name` is the sidebar row label (the 150px sidebar cannot fit the
// full satirical name); toasts use the full `name`.
struct BookableAct {
    const char* id;          // single-token E2E / lookup key, e.g. "cardboard"
    const char* name;        // full satirical name, shown in toasts
    const char* short_name;  // sidebar row label
    const char* genre;
    const char* flavor;  // one-line satirical blurb (booking tooltip)
    int fee;             // cash paid at booking time, never refunded
    float draw_mult;     // >1 draws a bigger crowd the next day
    float fun_mult;      // >1 makes stage-watching last longer that day
    int min_tier;        // festival tier required to book
};

inline constexpr BookableAct BOOKABLE_ACTS[] = {
    {"cardboard", "DJ Cardboard Box", "Cardboard", "Cardboard Techno",
     "Plays bangers off a Bluetooth speaker taped to a milk crate.", 500, 1.25f,
     1.25f, 0},
    {"poets", "The Port-a-Potty Poets", "PottyPoets", "Restroom Slam Poetry",
     "Rhymes about queues. The crowd weeps. Then queues again.", 800, 1.40f,
     1.30f, 0},
    {"moldy", "Moldy Lunchbox", "Moldy", "Garage Punk",
     "Three chords, one sandwich, zero rehearsals.", 1200, 1.60f, 1.40f, 1},
    {"refunds", "Sir Sings-A-Lot & The Refunds", "Sings-A-Lot",
     "Dad-Rock Covers",
     "Every chorus ends with a refund request. The crowd sings along.", 2000,
     1.90f, 1.50f, 1},
    {"karen", "KAREN & The Managers", "KAREN", "Complaint Metal",
     "Demands to speak to the festival. The festival apologises.", 3500, 2.40f,
     1.60f, 2},
    {"algorithm", "The Algorithm (Live)", "Algorithm", "AI Hyperpop",
     "Trained on your search history. It knows what you did.", 5000, 3.00f,
     1.80f, 2},
    {"gregonce", "Gregoncé", "Gregoncé", "Nepo-Pop",
     "Beyonce's cousin. Legally distinct. Vocally optimistic.", 8000, 4.00f,
     2.00f, 3},
    {"headliner", "The Headliner Who Shall Not Be Named", "Headliner",
     "Mystery Headliner", "Nobody knows who it is. Everybody paid anyway.",
     12000, 5.00f, 2.20f, 3},
};
inline constexpr int NUM_BOOKABLE_ACTS =
    static_cast<int>(sizeof(BOOKABLE_ACTS) / sizeof(BOOKABLE_ACTS[0]));

// Festival tiers: f(cumulative paid attendees, GameState::total_arrivals).
// Thresholds are data, not code: tier N is reached at
// FESTIVAL_TIER_THRESHOLDS[N - 1] attendees.
inline constexpr int FESTIVAL_TIER_THRESHOLDS[] = {500, 2000, 8000};
inline constexpr const char* FESTIVAL_TIER_NAMES[] = {"Local Fair", "Regional",
                                                      "National", "Legendary"};
inline constexpr const char* FESTIVAL_TIER_UP_LINES[] = {
    "", "The county paper noticed you exist.",
    "Truck drivers are rerouting for this.",
    "Historians are already writing about this weekend."};

inline int festival_tier_for_attendees(int total_arrivals) {
    int tier = 0;
    for (int t : FESTIVAL_TIER_THRESHOLDS)
        if (total_arrivals >= t) tier++;
    return tier;
}

// Inspection grants scale with tier: base * (1 + tier), so tier 0 keeps the
// historical $500 / $250 and tier 3 pays $2000 / $1000.
inline int inspection_grant_for(char grade, int tier) {
    if (grade == 'A') return INSPECTION_GRANT_A * (1 + tier);
    if (grade == 'B') return INSPECTION_GRANT_B * (1 + tier);
    return 0;
}

// Max simultaneous agents the automatic spawner will maintain: +25 per tier.
inline constexpr int LINEUP_BASE_SPAWN_CAP = 200;
inline constexpr int LINEUP_SPAWN_CAP_PER_TIER = 25;
inline int max_spawn_cap_for_tier(int tier) {
    return LINEUP_BASE_SPAWN_CAP + LINEUP_SPAWN_CAP_PER_TIER * tier;
}

// Lineup state singleton (lives on the Sophie entity).
// `booked` plays NEXT day, `active` plays TODAY. At each day rollover the
// booked acts are promoted to active and yesterday's act effects expire
// (active is replaced, not appended). Multiple booked acts play at once —
// one per stage — and their draw/fun stack additively:
// combined = 1 + sum(each - 1), so two acts at 1.25 / 1.40 draw 1.65.
struct LineupState : afterhours::BaseComponent {
    int tier = 0;
    int last_rollover_day = 1;  // day_number the rollover last ran on
    std::vector<int> booked;    // BOOKABLE_ACTS indices, for the next day
    std::vector<int> active;    // BOOKABLE_ACTS indices, playing today

    bool is_booked(int idx) const {
        return std::find(booked.begin(), booked.end(), idx) != booked.end();
    }
    bool is_active(int idx) const {
        return std::find(active.begin(), active.end(), idx) != active.end();
    }
    bool is_unlocked(int idx) const {
        if (idx < 0 || idx >= NUM_BOOKABLE_ACTS) return false;
        return tier >= BOOKABLE_ACTS[idx].min_tier;
    }
    float draw_mult() const {
        float combined = 1.0f;
        for (int idx : active) combined += BOOKABLE_ACTS[idx].draw_mult - 1.0f;
        return combined;
    }
    float fun_mult() const {
        float combined = 1.0f;
        for (int idx : active) combined += BOOKABLE_ACTS[idx].fun_mult - 1.0f;
        return combined;
    }
    std::string names_for(const std::vector<int>& list) const {
        std::string out;
        for (int idx : list) {
            if (!out.empty()) out += ", ";
            out += BOOKABLE_ACTS[idx].name;
        }
        return out;
    }
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

// Staff roles. Staff are real entities (Transform + StaffMember), not
// abstract counters: guards patrol, medics walk to collapsed agents,
// janitors roam. They have no Agent component, so they are never counted
// as attendees, never take crush damage, and never join the density map.
enum class StaffRole { Guard, Medic, Janitor };

struct StaffMember : afterhours::BaseComponent {
    StaffRole role = StaffRole::Guard;
    int home_x = 0;  // hire tile: patrol stays within STAFF_PATROL_RADIUS
    int home_z = 0;
    int target_x = -1;  // current wander/seek target tile (-1 = repick)
    int target_z = -1;
    int move_x = -1;  // next step tile (greedy pathfinding cache)
    int move_z = -1;
};

// Payroll / quit bookkeeping singleton (lives on the Sophie entity).
// Counts are derived live from StaffMember entities; this only tracks
// payroll edges (which day was last paid, cumulative quits for the
// ticker) so a reset can resync without re-reporting old walkouts.
struct StaffState : afterhours::BaseComponent {
    int last_payroll_day = 1;
    int quit_count = 0;  // cumulative staff who quit over unpaid wages
};

inline int staff_bonus_for(StaffRole role) {
    switch (role) {
        case StaffRole::Guard:
            return STAFF_GUARD_BONUS;
        case StaffRole::Medic:
            return STAFF_MEDIC_BONUS;
        case StaffRole::Janitor:
            return STAFF_JANITOR_BONUS;
    }
    return 0;
}

inline int staff_wage_for(StaffRole role) {
    switch (role) {
        case StaffRole::Guard:
            return STAFF_GUARD_WAGE;
        case StaffRole::Medic:
            return STAFF_MEDIC_WAGE;
        case StaffRole::Janitor:
            return STAFF_JANITOR_WAGE;
    }
    return 0;
}

inline const char* staff_name_for(StaffRole role) {
    switch (role) {
        case StaffRole::Guard:
            return "Guard";
        case StaffRole::Medic:
            return "Medic";
        case StaffRole::Janitor:
            return "Janitor";
    }
    return "Staff";
}

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

// Death location marker (fades over time)
struct DeathMarker : afterhours::BaseComponent {
    vec2 position;
    float lifetime = 10.0f;
    float max_lifetime = 10.0f;
};

// NUX (New User Experience) hint — persists until dismissed or completed
struct NuxHint : afterhours::BaseComponent {
    std::string text;
    bool is_active = false;
    bool was_dismissed = false;
    float time_shown = 0.f;

    std::function<bool()> should_trigger;
    std::function<bool()> is_complete;

    int order = 0;
};

// NUX manager singleton — tracks global state for the hint queue
struct NuxManager : afterhours::BaseComponent {
    bool initialized = false;

    // Bottleneck tracking (once per facility type per run)
    bool bathroom_warned = false;
    bool food_warned = false;
    bool medtent_warned = false;
    float bathroom_overload_timer = 0.f;
    float food_overload_timer = 0.f;
    float medtent_overload_timer = 0.f;
};

// LOD level for agent rendering based on camera zoom
enum class LODLevel { Close, Medium, Far };

// Visible tile region computed once per frame for culling + LOD
struct VisibleRegion : afterhours::BaseComponent {
    int min_x = 0, max_x = MAP_SIZE - 1;
    int min_z = 0, max_z = MAP_SIZE - 1;
    LODLevel lod = LODLevel::Close;
    float fovy = 5.0f;
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
