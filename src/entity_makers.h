#pragma once

// Must set AFTER_HOURS_REPLACE_LOGGING and include log.h BEFORE any afterhours
// headers so our log macros are used instead of the vendor's log functions
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "components.h"

// Sophie - central entity holding all singletons
afterhours::Entity& make_sophie();

// Agents (minimal - spawned at grid position)
afterhours::Entity& make_agent(int grid_x, int grid_z, FacilityType want);

// Check if escape should quit
bool should_escape_quit();
