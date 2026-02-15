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
void register_mcp_update_systems(SystemManager& sm);
void register_mcp_render_systems(SystemManager& sm);
void register_e2e_systems(SystemManager& sm);

inline void register_all_systems(SystemManager& sm) {
    // Input system runs first to collect inputs
    afterhours::input::register_update_systems(sm);

    register_mcp_update_systems(sm);
    register_update_systems(sm);

    // Register E2E command handlers when in test mode
    if (g_test_mode) {
        register_e2e_systems(sm);
    }

    register_render_systems(sm);
    register_mcp_render_systems(sm);
}
