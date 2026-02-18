// Render system orchestrator — delegates to domain registration functions.
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "systems.h"

// Domain registration functions (defined in separate .cpp files)
void register_render_world_systems(SystemManager& sm);
void register_render_ui_systems(SystemManager& sm);
void register_render_debug_systems(SystemManager& sm);
void register_render_end_system(SystemManager& sm);

void register_render_systems(SystemManager& sm) {
    // 3D world pass (grid, glow, agents, overlays, particles, preview)
    register_render_world_systems(sm);

    // 2D UI pass (hover, labels, HUD, timeline, minimap, game over)
    register_render_ui_systems(sm);

    // Debug panel (sliders)
    register_render_debug_systems(sm);

    // End render (flush texture to screen)
    register_render_end_system(sm);
}
