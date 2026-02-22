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
// Read by AgentMovementSystem (rain) and NeedTickSystem (heat).
namespace event_flags {
inline bool rain_active = false;
inline bool heat_active = false;
}  // namespace event_flags

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
// Facilities (Bathroom, Food, MedTent) are walkable so agents can enter them.
// Indexed by TileType enum: Grass=0,Path=1,Fence=2,Gate=3,Stage=4,
//   StageFloor=5,Bathroom=6,Food=7,MedTent=8
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
};

inline bool tile_blocks_movement(TileType type) {
    return TILE_BLOCKS[static_cast<int>(type)];
}

// Map FacilityType to pheromone channel index.
// FacilityType enum order matches Tile::PHERO_* constants exactly.
inline int facility_to_channel(FacilityType type) {
    return static_cast<int>(type);
}
