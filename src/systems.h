#pragma once

#include "afterhours/src/core/system.h"

using namespace afterhours;

void register_update_systems(SystemManager& sm);
void register_render_systems(SystemManager& sm);
void register_mcp_update_systems(SystemManager& sm);
void register_mcp_render_systems(SystemManager& sm);

// Call after creating path nodes to precompute signposts
void calculate_path_signposts();

inline void register_all_systems(SystemManager& sm) {
    register_mcp_update_systems(sm);
    register_update_systems(sm);
    register_render_systems(sm);
    register_mcp_render_systems(sm);
}
