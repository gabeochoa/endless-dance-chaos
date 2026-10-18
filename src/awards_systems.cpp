// Park awards + park history: RCT2-style awards evaluated at each day
// rollover from the day that just ended, plus the Finances-style
// history record for rating + attendees (see ParkAwardState in
// components.h for the data table).
//
// Evaluation order is the table order; within a slot a later
// qualifying award replaces an earlier one on the same day ("latest
// wins"), and across days a new qualifier replaces the held award. A
// day with no qualifier leaves the held award in place.
//
// Ordering: AwardsSystem registers AFTER the tycoon systems (see
// register_update_systems), so the rollover frame's inspection,
// rating target, and need pressure are already updated when the
// closed day is evaluated and recorded.
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "afterhours/src/core/entity_query.h"
#include "components.h"
#include "systems.h"
#include "update_helpers.h"

// Announce on both channels (toast + NEWS ticker queue), like events
// and finance announcements: the ticker drains the queue once per
// render frame.
static void announce_award(const std::string& text) {
    spawn_toast(text, 5.0f);
    auto* director = EntityHelper::get_singleton_cmp<EventDirectorState>();
    if (director) {
        if (director->announcement_queue.size() > 16)
            director->announcement_queue.erase(
                director->announcement_queue.begin());
        director->announcement_queue.push_back(text);
    }
}

struct AwardsSystem : System<> {
    void once(float) override {
        if (skip_game_logic()) return;
        auto* awards = EntityHelper::get_singleton_cmp<ParkAwardState>();
        auto* diff = EntityHelper::get_singleton_cmp<DifficultyState>();
        if (!awards || !diff) return;

        // Track the current day's live-attendee peak every frame, so
        // the closed day's history row records the peak even if the
        // crowds left before rollover.
        int live = static_cast<int>(
            EntityQuery().whereHasComponent<Agent>().gen_count());
        awards->peak_attendees_today =
            std::max(awards->peak_attendees_today, live);

        int new_day = diff->day_number;
        if (new_day == awards->last_award_day &&
            new_day == awards->last_history_day)
            return;
        int closed_day = awards->last_award_day;
        awards->last_award_day = new_day;
        awards->last_history_day = new_day;
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        if (new_day <= closed_day) {
            // Day went backwards (E2E set_day_number): resync the
            // baselines so the next rollover does not double-count.
            if (gs) {
                awards->deaths_at_last = gs->death_count;
                awards->injuries_at_last = gs->injury_count;
            }
            awards->peak_attendees_today = live;
            return;
        }

        // ── History: close the books on the day that just ended ──
        if (auto* rating = EntityHelper::get_singleton_cmp<ParkRatingState>()) {
            ParkHistoryDay row{closed_day,
                               static_cast<int>(std::lround(rating->rating)),
                               awards->peak_attendees_today};
            awards->history.push_back(row);
            while (static_cast<int>(awards->history.size()) >
                   PARK_HISTORY_MAX_DAYS)
                awards->history.erase(awards->history.begin());
            log_info("Park history: Day {} rating {} attendees {}", row.day,
                     row.rating, row.attendees);
        }

        // ── Awards: evaluate the day that just ended ──
        int deaths_today = 0, injuries_today = 0;
        if (gs) {
            deaths_today = gs->death_count - awards->deaths_at_last;
            injuries_today = gs->injury_count - awards->injuries_at_last;
            awards->deaths_at_last = gs->death_count;
            awards->injuries_at_last = gs->injury_count;
        }
        float scenery = 0.f;
        if (auto* grid = EntityHelper::get_singleton_cmp<Grid>())
            scenery = scenery_coverage_fraction(*grid);
        float rating_val = 0.f, need_pressure = 0.f;
        if (auto* rating = EntityHelper::get_singleton_cmp<ParkRatingState>()) {
            rating_val = rating->rating;
            need_pressure = rating->need_pressure;
        }
        int ticket_price = DEFAULT_TICKET_PRICE;
        if (auto* econ = EntityHelper::get_singleton_cmp<Economy>())
            ticket_price = econ->ticket_price;
        int peak = awards->peak_attendees_today;
        awards->peak_attendees_today = live;

        int new_positive = awards->positive_id;
        int new_negative = awards->negative_id;
        // Positive, table order: later qualifiers replace earlier ones.
        if (deaths_today == 0 &&
            injuries_today <= PARK_AWARD_SAFEST_MAX_INJURIES)
            new_positive = 1;  // Safest Park
        if (scenery >= PARK_AWARD_BEAUTIFUL_MIN_SCENERY)
            new_positive = 2;  // Most Beautiful
        if (rating_val >= PARK_AWARD_BEST_VALUE_MIN_RATING &&
            ticket_price <= PARK_AWARD_BEST_VALUE_MAX_PRICE)
            new_positive = 3;  // Best Value
        if (peak >= PARK_AWARD_CROWD_MIN_PEAK)
            new_positive = 4;  // Crowd Magnet
        // Negative, table order: Danger Zone replaces Untidy.
        if (scenery <= PARK_AWARD_UNTIDY_MAX_SCENERY &&
            need_pressure >= PARK_AWARD_UNTIDY_MIN_NEED_PRESSURE)
            new_negative = 5;                    // Untidy
        if (deaths_today > 0) new_negative = 6;  // Danger Zone

        if (new_positive != awards->positive_id) {
            awards->positive_id = new_positive;
            if (const auto* def = park_award_def_for_id(new_positive)) {
                announce_award(fmt::format("AWARD! {} — {} {}", def->name,
                                           def->icon, def->blurb));
                log_info("Award (positive): {} (day {})", def->name,
                         closed_day);
            }
        }
        if (new_negative != awards->negative_id) {
            awards->negative_id = new_negative;
            if (const auto* def = park_award_def_for_id(new_negative)) {
                announce_award(fmt::format("AWARD! {} — {} {}", def->name,
                                           def->icon, def->blurb));
                log_info("Award (negative): {} (day {})", def->name,
                         closed_day);
            }
        }
    }
};

void register_awards_systems(SystemManager& sm) {
    sm.register_update_system(std::make_unique<AwardsSystem>());
}
