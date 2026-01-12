#pragma once

// Must set AFTER_HOURS_REPLACE_LOGGING and include log.h BEFORE any afterhours
// headers so our log macros are used instead of the vendor's log functions
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "components.h"

using namespace afterhours;

// Sophie - central entity holding all singletons
Entity& make_sophie();

// Facilities
Entity& make_stage(float x, float z);
Entity& make_bathroom(float x, float z);
Entity& make_food(float x, float z);

// Spawners
Entity& make_attraction(float x, float z, float spawn_rate = 5.0f,
                        int capacity = 100);

// Path tiles
Entity& make_path_tile(int grid_x, int grid_z);
void remove_path_tile_at(int grid_x, int grid_z);
bool find_path_tile_at(int grid_x, int grid_z);
void create_initial_path_layout();  // Pre-place starting paths

// Agents (spawned by attractions, but can be created manually)
Entity& make_agent(float x, float z, FacilityType want, int origin_id = -1);

// Calculate signposts after all paths are created
void calculate_path_signposts();
