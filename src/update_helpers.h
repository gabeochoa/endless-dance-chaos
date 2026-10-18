#pragma once

// Shared helpers used by all update system domain files.
// Provides common game-state queries, event flags, tile helpers, and toasts.

// AFTER_HOURS_REPLACE_LOGGING and log.h must come first
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "audio.h"
#include "components.h"

using namespace afterhours;

// Global event effect flags set by ApplyEventEffectsSystem each frame.
// Read by AgentMovementSystem (rain), NeedTickSystem (heat),
// ViralSpreadSystem (viral), and FacilityServiceSystem (offline).
namespace event_flags {
inline bool rain_active = false;
inline bool heat_active = false;
inline bool viral_active = false;
// EquipmentFailure: one flag per FacilityType, indexed by its value.
// Only Bathroom/Food/MedTent/Bar/WaterStation ever go offline.
inline std::array<bool, 7> facility_offline_flags = {};
}  // namespace event_flags

// True when the facility type is down for an Equipment Failure event.
inline bool facility_offline(FacilityType type) {
    int i = static_cast<int>(type);
    if (i < 0 ||
        i >= static_cast<int>(event_flags::facility_offline_flags.size()))
        return false;
    return event_flags::facility_offline_flags[static_cast<size_t>(i)];
}

inline bool game_is_over() {
    auto* gs = EntityHelper::get_singleton_cmp<GameState>();
    return gs && gs->is_game_over();
}

inline bool game_is_paused() {
    auto* clock = EntityHelper::get_singleton_cmp<GameClock>();
    return clock && clock->speed == GameSpeed::Paused;
}

inline bool skip_game_logic() { return game_is_over() || game_is_paused(); }

inline void spawn_toast(const std::string& text, float lifetime = 3.0f) {
    Entity& te = EntityHelper::createEntity();
    te.addComponent<ToastMessage>();
    auto& toast = te.get<ToastMessage>();
    toast.text = text;
    toast.lifetime = lifetime;
    EntityHelper::merge_entity_arrays();
    get_audio().play_toast();
}

// Lookup table: true if tile blocks agent movement (fences + stage structure).
// Facilities (Bathroom, Food, MedTent, Bar, SecurityPost, WaterStation) are
// walkable so agents can enter them. Locked (unbought land) blocks.
// Tree blocks (a trunk); Bench is walkable (agents sit on it).
// All three path types and Lamp are walkable.
// Indexed by TileType enum: Grass=0,Path=1,Fence=2,Gate=3,Stage=4,
//   StageFloor=5,Bathroom=6,Food=7,MedTent=8,Bar=9,SecurityPost=10,
//   WaterStation=11,Locked=12,Tree=13,Bench=14,DirtPath=15,NeonPath=16,
//   Lamp=17
inline constexpr bool TILE_BLOCKS[] = {
    false,  // Grass
    false,  // Path
    true,   // Fence
    false,  // Gate
    true,   // Stage
    false,  // StageFloor
    false,  // Bathroom
    false,  // Food
    false,  // MedTent
    false,  // Bar
    false,  // SecurityPost
    false,  // WaterStation
    true,   // Locked
    true,   // Tree
    false,  // Bench
    false,  // DirtPath
    false,  // NeonPath
    false,  // Lamp
};

inline bool tile_blocks_movement(TileType type) {
    return TILE_BLOCKS[static_cast<int>(type)];
}

// Map FacilityType to pheromone channel index.
// FacilityType enum order matches Tile::PHERO_* constants exactly for the
// original five types. Bar / WaterStation have no channel (-1); callers
// must guard before indexing Tile::pheromone.
inline int facility_to_channel(FacilityType type) {
    if (type == FacilityType::Bar || type == FacilityType::WaterStation)
        return -1;
    return static_cast<int>(type);
}

// True if any tile of the given type lies within radius of (x, z).
// Used for the Bar / Security Post / Water Station proximity auras.
inline bool near_tile_type(Grid& grid, int x, int z, TileType type,
                           float radius) {
    const auto& positions = grid.get_facility_positions(type);
    float r2 = radius * radius;
    for (auto [px, pz] : positions) {
        float dx = static_cast<float>(px - x);
        float dz = static_cast<float>(pz - z);
        if (dx * dx + dz * dz <= r2) return true;
    }
    return false;
}
