#pragma once

#include "afterhours/src/core/entity_helper.h"
#include "components.h"

using namespace afterhours;

// Facilities
Entity& make_stage(float x, float z);
Entity& make_bathroom(float x, float z);
Entity& make_food(float x, float z);

// Spawners
Entity& make_attraction(float x, float z, float spawn_rate = 5.0f,
                        int capacity = 100);

// Path nodes
Entity& make_path_node(float x, float z, int next_node_id = -1,
                       float width = 1.5f);
Entity& make_hub(float x, float z);  // Path node with no next (junction point)

// Agents (spawned by attractions, but can be created manually)
Entity& make_agent(float x, float z, FacilityType want, int origin_id = -1);

// Camera
Entity& make_camera();

// Game state singleton
Entity& make_game_state();

// Calculate signposts after all paths are created
void calculate_path_signposts();
