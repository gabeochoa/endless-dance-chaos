#pragma once

// AFTER_HOURS_REPLACE_LOGGING and log.h must come first
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/system.h"
#include "afterhours/src/plugins/input_system.h"

// Forward-declared: including components.h here would pull rl.h in
// before afterhours' own headers (vendor type errors in render_systems).
#include <string>
#include <vector>
enum class StaffRole;
enum class EventType;
enum class FacilityType;
struct ScenarioDef;

using namespace afterhours;

// Test mode flag - set by main.cpp when --test-mode is passed
extern bool g_test_mode;

void register_update_systems(SystemManager& sm);
void register_render_systems(SystemManager& sm);

// Pick the closest non-crowded StageFloor tile to (from_x, from_z)
std::pair<int, int> best_stage_spot(int from_x, int from_z);
// Run one health-and-safety inspection now (tycoon_systems.cpp). Shared by
// the daily inspection trigger and the E2E run_inspection command.
void run_inspection_now();
// Buy one land-expansion plot: axis 0 = East (+x), 1 = South (+z).
// Charges Grid::next_expansion_cost() first; refused (no charge) at the
// per-axis cap or when unaffordable. Shared by the land panel and E2E.
bool buy_land_expansion(int axis);
void register_tycoon_systems(SystemManager& sm);
// Loans / finance history / cash-crisis guard (tycoon_systems.cpp).
// register_finance_systems runs AFTER StaffPayrollSystem: history and
// the crisis check see cash after payroll. Borrow / repay share the
// Finances-tab buttons and the E2E commands.
void register_finance_systems(SystemManager& sm);
bool borrow_loan(int amount);
bool repay_loan(int amount);
bool repay_loan_all();
// E2E-only: deduct the day's wages without the payroll quit-out, so a
// test can drive cash negative via wages and exercise the crisis guard.
void charge_wages_unchecked();
// Park awards + park history (awards_systems.cpp): evaluated at each
// day rollover, after the tycoon rating / finance systems so the
// closed day's rating, inspection, and needs are already updated.
void register_awards_systems(SystemManager& sm);
void register_lineup_systems(SystemManager& sm);
void register_staff_systems(SystemManager& sm);
// Lineup booking (lineup_systems.cpp): book for the NEXT day, fee paid now.
// Returns false (fee kept) when locked, already booked, or unaffordable.
bool book_act(int act_idx);
bool book_act_by_id(const std::string& id);
int find_bookable_act_index(const std::string& id);
// Staff (staff_systems.cpp): hire deducts the signing bonus, fire refunds
// nothing, counts are live StaffMember entities. x/z = spawn tile.
bool hire_staff(StaffRole role, int x, int z);
bool fire_staff(StaffRole role);
int staff_count(StaffRole role);
int staff_total_wages();
// Facility service time multiplier from janitors: 1 - 0.1 each, min 0.5.
float staff_service_time_factor();
// True when a guard is within STAFF_GUARD_RADIUS of tile (gx, gz).
bool guard_near(int gx, int gz);
// True when a medic is within STAFF_MEDIC_RADIUS of tile (gx, gz).
bool medic_near(int gx, int gz);
// Scenarios (scenario_systems.cpp): RCT campaign goals. Selecting
// restarts objective tracking from the live park state and applies the
// scenario's starting cash; -1 clears the active scenario (no cash
// change). The objective-line / status / deadline helpers feed the Park
// tab and the E2E assertions.
const ScenarioDef* scenario_def_for(int id);
const ScenarioDef* scenario_def_for_key(const std::string& key);
bool select_scenario(int id);
bool select_scenario_by_key(const std::string& key);
int scenario_earned_progress();
int scenario_deaths_since_start();
std::vector<std::string> scenario_objective_lines();
std::string scenario_status_text();
std::string scenario_deadline_text();
std::string scenario_row_status_text(int id);
void register_scenario_systems(SystemManager& sm);
// Event director (event_systems.cpp): fire one event now. Timed events
// create an ActiveEvent; instant events (inspection / sponsor) apply
// immediately. `offline_type` is the EquipmentFailure facility (-1 =
// pick from the facility types present on the map, -2 = same pick via
// the director's RNG stream). Announces via toast + NEWS ticker queue.
// Does NOT touch the director's daily quota / no-repeat bookkeeping;
// the director records its own picks.
void fire_event(EventType type, float duration, int offline_type = -1);
// Fire the current day's remaining director quota right now (E2E
// roll_events): rolls the day's target first if the director has not
// rolled for this day yet. Returns events fired.
int director_roll_today();
// Seed the director's dedicated RNG (independent of the shared
// RandomEngine stream, so event picks are reproducible regardless of
// agent-movement draws). Also clears the director's day bookkeeping.
void set_event_seed(const std::string& seed);
// Reset the director RNG to "default_seed" and clear the director
// singleton (called by reset_game_state).
void reset_event_director();
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
