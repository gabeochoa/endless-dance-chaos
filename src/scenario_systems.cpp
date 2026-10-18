// Scenario domain: RCT campaign goals (see SCENARIO_DEFS in components.h).
// Selecting a scenario restarts objective tracking from the current park
// state (new baselines, peaks reset) and applies the scenario's starting
// cash; the park itself — tiles, agents, day — is untouched. Completing
// every objective wins (banner toast + ticker + cash prize); a death
// where no deaths are allowed fails at once, anything else fails at the
// deadline. Sandbox play continues freely after either terminal state.
//
// Ordering: ScenarioSystem registers BEFORE the tycoon Inspection /
// ParkRating systems (see register_update_systems), so a deadline
// evaluation sees the inspection state as of the previous day (a new
// day's inspection is that new day's, outside the old window) and sees
// the rating before this frame's decay pulls a just-set 600 under.
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "afterhours/src/core/entity_query.h"
#include "components.h"
#include "systems.h"
#include "update_helpers.h"

const ScenarioDef* scenario_def_for(int id) {
    if (id < 0 || id >= NUM_SCENARIOS) return nullptr;
    return &SCENARIO_DEFS[id];
}

// Normalize a scenario key for lookup: lowercase, '_' and '-' dropped,
// so "first_weekend", "firstweekend", "first-weekend" all match.
static std::string normalize_scenario_key(const std::string& key) {
    std::string out;
    for (char c : key) {
        if (c == '_' || c == '-') continue;
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

const ScenarioDef* scenario_def_for_key(const std::string& key) {
    std::string want = normalize_scenario_key(key);
    for (const auto& def : SCENARIO_DEFS)
        if (normalize_scenario_key(def.key) == want) return &def;
    return nullptr;
}

// Select a scenario: restart tracking from the live park state and apply
// the starting cash. id -1 clears the active scenario (no cash change).
bool select_scenario(int id) {
    auto* st = EntityHelper::get_singleton_cmp<ScenarioState>();
    if (!st) return false;
    if (id == -1) {
        st->active_id = -1;
        st->status = ScenarioStatus::Inactive;
        log_info("Scenario cleared");
        return true;
    }
    const ScenarioDef* def = scenario_def_for(id);
    if (!def) return false;
    auto* diff = EntityHelper::get_singleton_cmp<DifficultyState>();
    auto* econ = EntityHelper::get_singleton_cmp<Economy>();
    auto* gs = EntityHelper::get_singleton_cmp<GameState>();
    auto* insp = EntityHelper::get_singleton_cmp<InspectionState>();
    st->active_id = id;
    st->status = ScenarioStatus::Active;
    st->start_day = diff ? diff->day_number : 1;
    st->start_earned = econ ? econ->total_earned : 0;
    st->start_deaths = gs ? gs->death_count : 0;
    st->start_inspections = insp ? insp->inspections_done : 0;
    st->seen_inspections = st->start_inspections;
    st->peak_attendees = 0;
    st->peak_rating = 0.f;
    st->had_good_inspection = false;
    if (econ) econ->cash = def->starting_cash;
    spawn_toast(fmt::format("Scenario selected: {} — {}", def->name, def->goal),
                4.0f);
    log_info("Scenario selected: {} (start day {}, starting cash ${})",
             def->name, st->start_day, def->starting_cash);
    return true;
}

bool select_scenario_by_key(const std::string& key) {
    if (normalize_scenario_key(key) == "none") return select_scenario(-1);
    const ScenarioDef* def = scenario_def_for_key(key);
    if (!def) return false;
    return select_scenario(def->id);
}

// Earned since selection. While Won the prize (paid via Economy::earn)
// would otherwise inflate the counter past the target, so a won
// Tycoon counter reads exactly its target.
int scenario_earned_progress() {
    auto* st = EntityHelper::get_singleton_cmp<ScenarioState>();
    if (!st) return 0;
    const ScenarioDef* def = st->def();
    if (!def || def->earned_target <= 0) return 0;
    if (st->status == ScenarioStatus::Won) return def->earned_target;
    auto* econ = EntityHelper::get_singleton_cmp<Economy>();
    int earned = econ ? econ->total_earned : 0;
    return std::max(0, earned - st->start_earned);
}

int scenario_deaths_since_start() {
    auto* st = EntityHelper::get_singleton_cmp<ScenarioState>();
    if (!st) return 0;
    auto* gs = EntityHelper::get_singleton_cmp<GameState>();
    int deaths = gs ? gs->death_count : 0;
    return std::max(0, deaths - st->start_deaths);
}

std::string scenario_status_text() {
    auto* st = EntityHelper::get_singleton_cmp<ScenarioState>();
    if (!st) return "Scenario: None";
    const ScenarioDef* def = st->def();
    if (!def) return "Scenario: None";
    switch (st->status) {
        case ScenarioStatus::Active:
            return fmt::format("Scenario: {} - Active", def->name);
        case ScenarioStatus::Won:
            return fmt::format("Scenario: {} - WON! +${}", def->name,
                               def->prize);
        case ScenarioStatus::Failed:
            return fmt::format("Scenario: {} - FAILED", def->name);
        case ScenarioStatus::Inactive:
            return "Scenario: None";
    }
    return "Scenario: None";
}

std::string scenario_deadline_text() {
    auto* st = EntityHelper::get_singleton_cmp<ScenarioState>();
    if (!st || !st->def()) return "";
    auto* diff = EntityHelper::get_singleton_cmp<DifficultyState>();
    int day = diff ? diff->day_number : 1;
    int left = st->deadline_day() - day;
    if (left <= 0)
        return fmt::format("Deadline: Day {} (last day)", st->deadline_day());
    return fmt::format("Deadline: Day {} ({} day{} left)", st->deadline_day(),
                       left, left == 1 ? "" : "s");
}

std::string scenario_row_status_text(int id) {
    auto* st = EntityHelper::get_singleton_cmp<ScenarioState>();
    if (!st || st->active_id != id) return "-";
    switch (st->status) {
        case ScenarioStatus::Active:
            return "Active";
        case ScenarioStatus::Won:
            return "WON";
        case ScenarioStatus::Failed:
            return "FAILED";
        case ScenarioStatus::Inactive:
            return "-";
    }
    return "-";
}

// Live objective checklist for the active scenario. A box reads [x] once
// its threshold is met (no-deaths / survive read [x] only on WON: they
// are pending, not complete, while the run is still going).
std::vector<std::string> scenario_objective_lines() {
    auto* st = EntityHelper::get_singleton_cmp<ScenarioState>();
    if (!st || !st->def()) return {"No scenario selected."};
    const ScenarioDef* def = st->def();
    bool won = st->status == ScenarioStatus::Won;
    std::vector<std::string> lines;
    if (def->attendees_target > 0) {
        bool done = st->peak_attendees >= def->attendees_target;
        lines.push_back(fmt::format("[{}] Attendees: {}/{}", done ? "x" : " ",
                                    st->peak_attendees, def->attendees_target));
    }
    if (def->rating_target > 0) {
        bool done =
            static_cast<int>(std::round(st->peak_rating)) >= def->rating_target;
        lines.push_back(fmt::format(
            "[{}] Rating: {}/{}", done ? "x" : " ",
            static_cast<int>(std::round(st->peak_rating)), def->rating_target));
    }
    if (def->earned_target > 0) {
        bool done = scenario_earned_progress() >= def->earned_target;
        lines.push_back(fmt::format("[{}] Earned: ${}/${}", done ? "x" : " ",
                                    scenario_earned_progress(),
                                    def->earned_target));
    }
    if (def->requires_survive) {
        lines.push_back(fmt::format("[{}] Survive to Day {}", won ? "x" : " ",
                                    st->deadline_day()));
    }
    if (def->requires_no_deaths) {
        int deaths = scenario_deaths_since_start();
        if (deaths > 0)
            lines.push_back(fmt::format("[ ] No deaths ({} death{})", deaths,
                                        deaths == 1 ? "" : "s"));
        else
            lines.push_back(fmt::format("[{}] No deaths", won ? "x" : " "));
    }
    if (def->requires_good_inspection) {
        lines.push_back(fmt::format("[{}] Inspection B+",
                                    st->had_good_inspection ? "x" : " "));
    }
    return lines;
}

// Announce on both channels (toast banner + NEWS ticker queue), like
// finance announcements.
static void announce_scenario(const std::string& text) {
    spawn_toast(text, 8.0f);
    auto* director = EntityHelper::get_singleton_cmp<EventDirectorState>();
    if (director) {
        if (director->announcement_queue.size() > 16)
            director->announcement_queue.erase(
                director->announcement_queue.begin());
        director->announcement_queue.push_back(text);
    }
}

struct ScenarioSystem : System<> {
    void once(float) override {
        if (skip_game_logic()) return;
        auto* st = EntityHelper::get_singleton_cmp<ScenarioState>();
        if (!st || st->status != ScenarioStatus::Active) return;
        const ScenarioDef* def = st->def();
        if (!def) return;

        // Track live peaks (observed after selection only).
        int live = static_cast<int>(
            EntityQuery().whereHasComponent<Agent>().gen_count());
        st->peak_attendees = std::max(st->peak_attendees, live);
        if (auto* rating = EntityHelper::get_singleton_cmp<ParkRatingState>())
            st->peak_rating = std::max(st->peak_rating, rating->rating);

        // Latch B+ inspections fired since selection (edge on
        // inspections_done, so a pre-selection grade never counts).
        if (auto* insp = EntityHelper::get_singleton_cmp<InspectionState>()) {
            if (insp->inspections_done > st->seen_inspections) {
                st->seen_inspections = insp->inspections_done;
                if (insp->last_grade == 'A' || insp->last_grade == 'B')
                    st->had_good_inspection = true;
            }
        }

        auto* diff = EntityHelper::get_singleton_cmp<DifficultyState>();
        int day = diff ? diff->day_number : 1;
        bool past_deadline = day >= st->deadline_day();
        bool died =
            def->requires_no_deaths && scenario_deaths_since_start() > 0;

        if (def->requires_survive) {
            if (died) return fail(*st, *def, "A guest died");
            if (past_deadline) {
                if (st->had_good_inspection) return win(*st, *def);
                return fail(*st, *def, "Survived, but no B+ inspection");
            }
            return;
        }
        if (died) return fail(*st, *def, "A guest died");
        bool all_done = true;
        if (def->attendees_target > 0 &&
            st->peak_attendees < def->attendees_target)
            all_done = false;
        if (def->rating_target > 0 &&
            static_cast<int>(std::round(st->peak_rating)) < def->rating_target)
            all_done = false;
        if (def->earned_target > 0 &&
            scenario_earned_progress() < def->earned_target)
            all_done = false;
        if (all_done) return win(*st, *def);
        if (past_deadline) return fail(*st, *def, "Deadline passed");
    }

    void win(ScenarioState& st, const ScenarioDef& def) {
        st.status = ScenarioStatus::Won;
        if (auto* econ = EntityHelper::get_singleton_cmp<Economy>())
            econ->earn(def.prize);
        announce_scenario(
            fmt::format("SCENARIO WON: {}! Prize: +${}.", def.name, def.prize));
        log_info("Scenario WON: {} (prize ${})", def.name, def.prize);
    }

    void fail(ScenarioState& st, const ScenarioDef& def,
              const std::string& reason) {
        st.status = ScenarioStatus::Failed;
        announce_scenario(fmt::format(
            "SCENARIO FAILED: {} - {}. Sandbox continues.", def.name, reason));
        log_info("Scenario FAILED: {} ({})", def.name, reason);
    }
};

void register_scenario_systems(SystemManager& sm) {
    sm.register_update_system(std::make_unique<ScenarioSystem>());
}
