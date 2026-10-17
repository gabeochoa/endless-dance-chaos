// Lineup domain: bookable acts, festival tiers, day-rollover promotion.
//
// Day-rollover sequence (order matters — this system sits between them):
//   1. DifficultyScalingSystem bumps DifficultyState::day_number.
//   2. LineupSystem (here): promotes booked acts to active (yesterday's
//      active acts expire), recomputes the tier from cumulative
//      attendees, and divides the spawn interval by the active draw.
//      Booking fees were already paid at booking time, so rollover
//      never charges again.
//   3. Tycoon InspectionSystem: inspection on the new day, grant scaled
//      by the NEW tier (this system ran first).
//   4. StaffPayrollSystem: payroll on the new day.
// Tier itself is recomputed every frame, not only at rollover, so crossing
// a threshold mid-day unlocks acts immediately.
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "afterhours/src/core/entity_query.h"
#include "components.h"
#include "systems.h"
#include "update_helpers.h"

static std::string lowercased(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

int find_bookable_act_index(const std::string& id) {
    std::string needle = lowercased(id);
    for (int i = 0; i < NUM_BOOKABLE_ACTS; i++)
        if (lowercased(BOOKABLE_ACTS[i].id) == needle) return i;
    return -1;
}

// Book an act for the NEXT day. Fee is paid now; refused (fee kept, no
// booking) when the act is locked, already booked for tomorrow, or
// unaffordable. An act already playing today CAN be re-booked for
// tomorrow: that is a new performance, charged again.
bool book_act(int idx) {
    auto* lineup = EntityHelper::get_singleton_cmp<LineupState>();
    auto* econ = EntityHelper::get_singleton_cmp<Economy>();
    if (!lineup || !econ) return false;
    if (idx < 0 || idx >= NUM_BOOKABLE_ACTS) return false;
    const auto& act = BOOKABLE_ACTS[idx];

    if (!lineup->is_unlocked(idx)) {
        spawn_toast(fmt::format("Locked: {} needs Tier {} ({} attendees).",
                                act.name, FESTIVAL_TIER_NAMES[act.min_tier],
                                FESTIVAL_TIER_THRESHOLDS[act.min_tier - 1]));
        log_info("Booking refused (locked): {} needs tier {}", act.name,
                 act.min_tier);
        return false;
    }
    if (lineup->is_booked(idx)) {
        spawn_toast(
            fmt::format("Already booked: {} plays tomorrow.", act.name));
        log_info("Booking refused (duplicate): {}", act.name);
        return false;
    }
    if (!econ->try_spend(act.fee)) {
        spawn_toast(
            fmt::format("Not enough cash! {} costs ${}", act.name, act.fee));
        log_warn("Booking refused: {} costs ${}, cash is ${}", act.name,
                 act.fee, econ->cash);
        return false;
    }
    lineup->booked.push_back(idx);
    spawn_toast(
        fmt::format("Booked {} for tomorrow! Fee ${}.", act.name, act.fee));
    log_info("Booked {} for tomorrow, fee ${}", act.name, act.fee);
    return true;
}

bool book_act_by_id(const std::string& id) {
    int idx = find_bookable_act_index(id);
    if (idx < 0) {
        spawn_toast(fmt::format("No such act: '{}'.", id));
        return false;
    }
    return book_act(idx);
}

struct LineupSystem : System<> {
    void once(float) override {
        if (skip_game_logic()) return;
        auto* lineup = EntityHelper::get_singleton_cmp<LineupState>();
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        auto* diff = EntityHelper::get_singleton_cmp<DifficultyState>();
        auto* spawn = EntityHelper::get_singleton_cmp<SpawnState>();
        if (!lineup || !gs || !diff) return;

        // Tier: f(cumulative attendees), checked every frame. A jump over
        // several thresholds in one frame announces once, at the new tier.
        int computed = festival_tier_for_attendees(gs->total_arrivals);
        if (computed > lineup->tier) {
            lineup->tier = computed;
            spawn_toast(fmt::format("TIER UP! Your festival is now {}! {}",
                                    FESTIVAL_TIER_NAMES[lineup->tier],
                                    FESTIVAL_TIER_UP_LINES[lineup->tier]),
                        5.0f);
            log_info("Tier up: {} ({} attendees)",
                     FESTIVAL_TIER_NAMES[lineup->tier], gs->total_arrivals);
        } else if (computed < lineup->tier) {
            // Counters only drop on reset; resync silently, no announcement.
            lineup->tier = computed;
        }

        // Rollover: booked (tomorrow) -> active (today); yesterday's
        // active acts expire by replacement, not by appending.
        if (diff->day_number != lineup->last_rollover_day) {
            int new_day = diff->day_number;
            lineup->last_rollover_day = new_day;
            if (new_day > 1) {
                lineup->active = lineup->booked;
                lineup->booked.clear();
                if (!lineup->active.empty()) {
                    spawn_toast(fmt::format("On stage today: {}.",
                                            lineup->names_for(lineup->active)),
                                5.0f);
                    log_info("Lineup active day {}: {}", new_day,
                             lineup->names_for(lineup->active));
                }
            }
        }

        // Spawn draw: runs after DifficultyScalingSystem each frame, which
        // resets the interval from its formula, so dividing once per frame
        // never compounds. Respects the debug manual override.
        if (spawn && !spawn->manual_override) {
            float draw = lineup->draw_mult();
            if (draw > 1.0f) spawn->interval /= draw;
        }
    }
};

void register_lineup_systems(SystemManager& sm) {
    sm.register_update_system(std::make_unique<LineupSystem>());
}
