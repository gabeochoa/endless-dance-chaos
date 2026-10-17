// Tycoon domain: ticket price keys, daily health-and-safety inspections,
// attendee milestones. Announcements use toasts, the existing channel for
// satirical news in this game (there is no scrolling ticker).
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "afterhours/src/core/entity_query.h"
#include "components.h"
#include "systems.h"
#include "update_helpers.h"

// Ticket price: "," / "." keys adjust by TICKET_PRICE_STEP, clamped [5,100].
// The HUD [-]/[+] buttons call the same Economy::adjust_price.
struct PriceAdjustSystem : System<> {
    void once(float) override {
        if (game_is_over()) return;
        auto* econ = EntityHelper::get_singleton_cmp<Economy>();
        if (!econ) return;
        if (action_pressed(InputAction::PriceUp)) {
            econ->adjust_price(Economy::TICKET_PRICE_STEP);
            log_info("Ticket price up: ${}", econ->ticket_price);
        }
        if (action_pressed(InputAction::PriceDown)) {
            econ->adjust_price(-Economy::TICKET_PRICE_STEP);
            log_info("Ticket price down: ${}", econ->ticket_price);
        }
    }
};

// Inspection scoring: 100 minus penalties for the record since the last
// inspection, minus a facility-coverage penalty. Grade bands: A >= 90,
// B >= 75, C >= 60, D >= 40, else F.
static char grade_for_score(int score) {
    if (score >= 90) return 'A';
    if (score >= 75) return 'B';
    if (score >= 60) return 'C';
    if (score >= 40) return 'D';
    return 'F';
}

static const char* inspection_line_for(char grade) {
    switch (grade) {
        case 'A':
            return "Inspector: 'Shockingly competent. The port-a-potties "
                   "sparkle.'";
        case 'B':
            return "Inspector: 'Adequate. Nobody died on MY watch.'";
        case 'C':
            return "Inspector: 'Smells like ambition and sunscreen.'";
        case 'D':
            return "Inspector: 'I have seen cleaner crime scenes.'";
        default:
            return "Inspector: 'SHUT IT DOWN. The health code has left the "
                   "chat.'";
    }
}

static int inspection_score(int deaths_since, int injuries_since,
                            int density_violations, int facility_tiles,
                            int max_attendees) {
    int score = 100;
    score -= 8 * deaths_since;
    score -= 3 * injuries_since;
    score -= std::min(20, density_violations / 5);
    // Coverage: one 2x2 facility per 100 peak attendees, counted in tiles.
    int expected_tiles = 4 * (1 + max_attendees / 100);
    int deficit = expected_tiles - facility_tiles;
    score -= std::clamp(deficit, 0, 20);
    return std::clamp(score, 0, 100);
}

// Run one inspection now; shared by the daily trigger and the E2E
// run_inspection command. Updates InspectionState, pays the A/B grant,
// and arms shutdown on F or a third consecutive D/F.
void run_inspection_now() {
    auto* insp = EntityHelper::get_singleton_cmp<InspectionState>();
    auto* gs = EntityHelper::get_singleton_cmp<GameState>();
    auto* grid = EntityHelper::get_singleton_cmp<Grid>();
    auto* econ = EntityHelper::get_singleton_cmp<Economy>();
    if (!insp || !gs) return;

    int deaths_since = gs->death_count - insp->deaths_at_last;
    int injuries_since = gs->injury_count - insp->injuries_at_last;

    int facility_tiles = 0;
    if (grid) {
        for (const auto& tile : grid->tiles) {
            switch (tile.type) {
                case TileType::Bathroom:
                case TileType::Food:
                case TileType::WaterStation:
                case TileType::MedTent:
                case TileType::Bar:
                    facility_tiles++;
                    break;
                default:
                    break;
            }
        }
    }

    int score =
        inspection_score(deaths_since, injuries_since, insp->density_violations,
                         facility_tiles, gs->max_attendees);
    char grade = grade_for_score(score);

    insp->last_grade = grade;
    insp->last_score = score;
    insp->inspections_done++;
    insp->deaths_at_last = gs->death_count;
    insp->injuries_at_last = gs->injury_count;
    insp->density_violations = 0;

    if (grade == 'D' || grade == 'F')
        insp->consecutive_bad++;
    else
        insp->consecutive_bad = 0;

    int grant = 0;
    if (grade == 'A') grant = INSPECTION_GRANT_A;
    if (grade == 'B') grant = INSPECTION_GRANT_B;
    if (grant > 0 && econ) econ->earn(grant);

    std::string msg =
        fmt::format("HEALTH INSPECTION: Grade {} ({}). {}",
                    std::string(1, grade), score, inspection_line_for(grade));
    if (grant > 0) msg += fmt::format(" Grant: +${}.", grant);
    spawn_toast(msg, 5.0f);
    log_info("Inspection #{}: grade {} score {} (deaths +{}, injuries +{})",
             insp->inspections_done, std::string(1, grade), score, deaths_since,
             injuries_since);

    if (grade == 'F' || insp->consecutive_bad >= 3) {
        insp->shutdown_triggered = true;
        spawn_toast(
            "FESTIVAL SHUT DOWN BY THE HEALTH INSPECTOR! Pack up the "
            "port-a-potties.",
            5.0f);
        log_info("Inspection shutdown: grade {} consecutive_bad={}", grade,
                 insp->consecutive_bad);
    }
}

// One inspection per game-day (on the day rollover detected by the
// DifficultyScalingSystem) plus the E2E force flag. Also samples density
// violations: one per second while any tile is critically dense.
struct InspectionSystem : System<> {
    float sample_timer = 0.f;

    void once(float dt) override {
        if (skip_game_logic()) return;
        auto* insp = EntityHelper::get_singleton_cmp<InspectionState>();
        auto* diff = EntityHelper::get_singleton_cmp<DifficultyState>();
        if (!insp || !diff) return;

        sample_timer += dt;
        if (sample_timer >= 1.0f) {
            sample_timer -= 1.0f;
            auto* grid = EntityHelper::get_singleton_cmp<Grid>();
            if (grid) {
                int critical =
                    static_cast<int>(DENSITY_CRITICAL * MAX_AGENTS_PER_TILE);
                for (const auto& tile : grid->tiles) {
                    if (tile.agent_count >= critical) {
                        insp->density_violations++;
                        break;
                    }
                }
            }
        }

        if (insp->force_inspection) {
            insp->force_inspection = false;
            run_inspection_now();
            return;
        }
        if (diff->day_number != insp->last_inspected_day) {
            int new_day = diff->day_number;
            insp->last_inspected_day = new_day;
            if (new_day > 1) run_inspection_now();
        }
    }
};

// Attendee milestones: 1k / 5k / 10k / 25k / 50k / 100k paid arrivals,
// each paying a cash bonus of half the milestone.
struct MilestoneSystem : System<> {
    void once(float) override {
        if (skip_game_logic()) return;
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        auto* econ = EntityHelper::get_singleton_cmp<Economy>();
        if (!gs || !econ) return;

        static constexpr int THRESHOLDS[] = {1000,  5000,  10000,
                                             25000, 50000, 100000};
        while (gs->milestone_idx < 6 &&
               gs->total_arrivals >= THRESHOLDS[gs->milestone_idx]) {
            int milestone = THRESHOLDS[gs->milestone_idx];
            int bonus = milestone / 2;
            gs->milestone_idx++;
            econ->earn(bonus);
            spawn_toast(
                fmt::format("MILESTONE: {} attendees! The mayor wants a "
                            "photo op. Bonus: +${}.",
                            milestone, bonus),
                5.0f);
            log_info("Milestone: {} attendees, bonus ${}", milestone, bonus);
        }
    }
};

void register_tycoon_systems(SystemManager& sm) {
    sm.register_update_system(std::make_unique<PriceAdjustSystem>());
    sm.register_update_system(std::make_unique<InspectionSystem>());
    sm.register_update_system(std::make_unique<MilestoneSystem>());
}
