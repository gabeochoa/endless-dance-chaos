#pragma once

// AFTER_HOURS_REPLACE_LOGGING and log.h must come first
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/system.h"
#include "afterhours/src/plugins/input_system.h"

using namespace afterhours;

// Test mode flag - set by main.cpp when --test-mode is passed
extern bool g_test_mode;

void register_update_systems(SystemManager& sm);
void register_render_systems(SystemManager& sm);

// Pick the closest non-crowded StageFloor tile to (from_x, from_z)
std::pair<int, int> best_stage_spot(int from_x, int from_z);
void register_mcp_update_systems(SystemManager& sm);
void register_mcp_render_systems(SystemManager& sm);
void register_e2e_systems(SystemManager& sm);

inline void register_all_systems(SystemManager& sm) {
    // Input system runs first to collect inputs
    afterhours::input::register_update_systems(sm);

    // E2E commands inject input, so they have to run alongside the real input
    // poll -- before anything reads it. Registered after the game systems, a
    // click injected mid-frame was read first by the UI render systems, which
    // consume the press edge; PathBuildSystem, running earlier, never saw it.
    if (g_test_mode) {
        register_e2e_systems(sm);
    }

    register_mcp_update_systems(sm);
    register_update_systems(sm);

    register_render_systems(sm);
    register_mcp_render_systems(sm);
}
