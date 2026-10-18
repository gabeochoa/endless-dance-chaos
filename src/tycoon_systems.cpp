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

    // Grant scales with the festival tier (LineupSystem runs before this
    // system in the rollover sequence, so this is already the new tier).
    int tier = 0;
    if (auto* lineup = EntityHelper::get_singleton_cmp<LineupState>())
        tier = lineup->tier;
    int grant = inspection_grant_for(grade, tier);
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

// ── Park rating + land expansion ─────────────────────────────────────────

// Exponential approach helper: 1 - exp(-rate * dt) is the fraction of
// the remaining gap closed this frame, so the rate is frame-rate
// independent.
static float approach_factor(float rate, float dt) {
    return 1.0f - std::exp(-rate * dt);
}

// Facility count for one type, in whole facilities: Bathroom / Food
// footprints are 2x2 (4 tiles), a Water Station is 1 tile.
static float facility_count_for(const Grid& grid, TileType type,
                                int tiles_per_facility) {
    int tiles = 0;
    for (const auto& tile : grid.tiles)
        if (tile.type == type) tiles++;
    return tiles / static_cast<float>(tiles_per_facility);
}

struct ParkRatingSystem : System<> {
    void once(float dt) override {
        if (skip_game_logic()) return;
        auto* rating = EntityHelper::get_singleton_cmp<ParkRatingState>();
        if (!rating) return;
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        auto* insp = EntityHelper::get_singleton_cmp<InspectionState>();
        auto* diff = EntityHelper::get_singleton_cmp<DifficultyState>();

        // Day rollover: yesterday = the rating at the start of the day
        // that just ended; the harm pool halves (deaths/injuries decay
        // over days, so the rating recovers).
        if (diff && diff->day_number != rating->last_day) {
            int old_day = rating->last_day;
            int new_day = diff->day_number;
            rating->last_day = new_day;
            if (new_day > old_day) {
                rating->yesterday_rating = rating->day_start_rating;
                rating->day_start_rating = rating->rating;
                rating->harm_pool *= PARK_RATING_HARM_DECAY_PER_DAY;
                rating->vip_bonus_pool *= PARK_RATING_HARM_DECAY_PER_DAY;
            } else {
                // Day went backwards (E2E set_day_number): resync flat.
                rating->yesterday_rating = rating->rating;
                rating->day_start_rating = rating->rating;
            }
        }

        // Deaths / injuries: instant rating drop + harm-pool charge.
        // Only positive deltas count (a reset lowers the counters).
        if (gs) {
            int new_deaths = gs->death_count - rating->last_death_count;
            int new_injuries = gs->injury_count - rating->last_injury_count;
            if (new_deaths > 0 || new_injuries > 0) {
                float harm = new_deaths * PARK_RATING_DEATH_PENALTY +
                             new_injuries * PARK_RATING_INJURY_PENALTY;
                rating->harm_pool =
                    std::min(PARK_RATING_HARM_CAP, rating->harm_pool + harm);
                rating->rating = std::clamp(
                    rating->rating - harm, static_cast<float>(PARK_RATING_MIN),
                    static_cast<float>(PARK_RATING_MAX));
            }
            rating->last_death_count = gs->death_count;
            rating->last_injury_count = gs->injury_count;
        }

        int live = static_cast<int>(
            EntityQuery().whereHasComponent<Agent>().gen_count());

        // Need satisfaction: fraction of live agents with any unmet need.
        // No penalty at or below the 0.25 baseline; ramps to the max at
        // every agent needy. EWMA so a one-frame blip does not swing it.
        float need_frac = 0.f;
        if (live > 0) {
            int needy = 0;
            auto agents =
                EntityQuery().whereHasComponent<Agent, AgentNeeds>().gen();
            for (Entity& a : agents) {
                auto& needs = a.get<AgentNeeds>();
                if (needs.needs_bathroom || needs.needs_food ||
                    needs.needs_heat || needs.needs_bar)
                    needy++;
            }
            need_frac = needy / static_cast<float>(live);
        }
        float instant_need = 0.f;
        if (need_frac > PARK_RATING_NEED_BASELINE) {
            instant_need = PARK_RATING_NEED_MAX_PENALTY *
                           (need_frac - PARK_RATING_NEED_BASELINE) /
                           (1.0f - PARK_RATING_NEED_BASELINE);
        }
        rating->need_pressure += (instant_need - rating->need_pressure) *
                                 approach_factor(PARK_RATING_PRESSURE_RATE, dt);

        // Critical-density time: fraction of live agents standing on
        // critically dense tiles (>= 36 agents). EWMA like needs.
        float crowd_frac = 0.f;
        if (live > 0 && grid) {
            int critical_threshold =
                static_cast<int>(DENSITY_CRITICAL * MAX_AGENTS_PER_TILE);
            int on_critical = 0;
            for (const auto& tile : grid->tiles)
                if (tile.agent_count >= critical_threshold)
                    on_critical += tile.agent_count;
            crowd_frac = std::min(1.0f, on_critical / static_cast<float>(live));
        }
        float instant_crowd = PARK_RATING_CROWDING_MAX_PENALTY * crowd_frac;
        rating->crowding_pressure +=
            (instant_crowd - rating->crowding_pressure) *
            approach_factor(PARK_RATING_PRESSURE_RATE, dt);

        // Facility coverage per 100 attendees: +40 per type (bathroom /
        // food / water) at >= 1 facility per 100 attendees, scaling
        // linearly below that. No attendees => no coverage bonus, so an
        // empty park sits exactly at the 500 base.
        float coverage = 0.f;
        if (live > 0 && grid) {
            float ideal = live * PARK_RATING_COVERAGE_IDEAL_PER_100 / 100.0f;
            if (ideal > 0.f) {
                float bathroom =
                    facility_count_for(*grid, TileType::Bathroom, 4);
                float food = facility_count_for(*grid, TileType::Food, 4);
                float water =
                    facility_count_for(*grid, TileType::WaterStation, 1);
                for (float facilities : {bathroom, food, water})
                    coverage += PARK_RATING_COVERAGE_PER_TYPE *
                                std::min(1.0f, facilities / ideal);
            }
        }

        // Scenery coverage: fraction of playable tiles within
        // PARK_RATING_SCENERY_RADIUS of a Tree, scaled to
        // PARK_RATING_SCENERY_MAX. Lamps count at LAMP_SCENERY_WEIGHT
        // (half a tree); a tile covered by both caps at 1. Applies
        // with or without attendees: a beautiful empty park still
        // reads as beautiful.
        float scenery = 0.f;
        if (grid) {
            grid->ensure_caches();
            const auto& trees = grid->tree_positions;
            const auto& lamps = grid->lamp_positions;
            if (!trees.empty() || !lamps.empty()) {
                std::array<float, MAP_SIZE * MAP_SIZE> covered{};
                covered.fill(0.f);
                int r = static_cast<int>(std::ceil(PARK_RATING_SCENERY_RADIUS));
                auto mark_covered = [&](const auto& positions, float weight) {
                    for (auto [tx, tz] : positions) {
                        for (int dz = -r; dz <= r; dz++) {
                            for (int dx = -r; dx <= r; dx++) {
                                if (dx * dx + dz * dz >
                                    PARK_RATING_SCENERY_RADIUS *
                                        PARK_RATING_SCENERY_RADIUS)
                                    continue;
                                int x = tx + dx, z = tz + dz;
                                if (!grid->in_bounds(x, z) ||
                                    !grid->in_playable(x, z))
                                    continue;
                                int idx = z * MAP_SIZE + x;
                                covered[idx] =
                                    std::min(1.f, covered[idx] + weight);
                            }
                        }
                    }
                };
                mark_covered(trees, 1.f);
                mark_covered(lamps, LAMP_SCENERY_WEIGHT);
                float covered_count = 0.f;
                for (float c : covered) covered_count += c;
                int playable = (grid->play_max_x() - PLAY_MIN + 1) *
                               (grid->play_max_z() - PLAY_MIN + 1);
                if (playable > 0)
                    scenery = PARK_RATING_SCENERY_MAX * covered_count /
                              static_cast<float>(playable);
            }
        }

        float inspection =
            insp ? park_rating_inspection_bonus(insp->last_grade) : 0.f;
        rating->target =
            std::clamp(PARK_RATING_BASE + coverage + scenery + inspection +
                           rating->vip_bonus_pool - rating->harm_pool -
                           rating->need_pressure - rating->crowding_pressure,
                       static_cast<float>(PARK_RATING_MIN),
                       static_cast<float>(PARK_RATING_MAX));

        rating->rating += (rating->target - rating->rating) *
                          approach_factor(PARK_RATING_APPROACH_RATE, dt);
        rating->rating =
            std::clamp(rating->rating, static_cast<float>(PARK_RATING_MIN),
                       static_cast<float>(PARK_RATING_MAX));
    }
};

// Buy one land-expansion plot East (axis 0) or South (axis 1). The
// caller-facing economy lives here; Grid::expand only moves the fence.
// Refused (no charge) at the per-axis cap or when unaffordable.
bool buy_land_expansion(int axis) {
    auto* grid = EntityHelper::get_singleton_cmp<Grid>();
    auto* econ = EntityHelper::get_singleton_cmp<Economy>();
    if (!grid || !econ) return false;
    if (axis != 0 && axis != 1) return false;
    if ((axis == 0 && grid->expansions_x >= EXPANSION_MAX_PER_AXIS) ||
        (axis == 1 && grid->expansions_z >= EXPANSION_MAX_PER_AXIS)) {
        spawn_toast(
            fmt::format("No more land to buy {}: that side is maxed out.",
                        axis == 0 ? "to the East" : "to the South"));
        log_info("Land expansion refused: axis {} at cap", axis);
        return false;
    }
    int cost = grid->next_expansion_cost();
    if (!econ->try_spend(cost)) {
        spawn_toast(
            fmt::format("Not enough cash! Land expansion costs ${}", cost));
        log_warn("Land expansion refused: costs ${}, cash is ${}", cost,
                 econ->cash);
        return false;
    }
    if (!grid->expand(axis)) {
        // Unreachable: the cap was checked above. Refund exactly.
        econ->cash += cost;
        econ->total_spent -= cost;
        log_warn("Land expansion failed after payment, refunded ${}", cost);
        return false;
    }
    spawn_toast(fmt::format("Land expanded {}! +{} buildable tiles for ${}.",
                            axis == 0 ? "East" : "South", EXPANSION_TILES,
                            cost));
    log_info("Land expanded axis {} for ${} (total expansions {})", axis, cost,
             grid->total_expansions());
    return true;
}

void register_tycoon_systems(SystemManager& sm) {
    sm.register_update_system(std::make_unique<PriceAdjustSystem>());
    sm.register_update_system(std::make_unique<InspectionSystem>());
    sm.register_update_system(std::make_unique<MilestoneSystem>());
    sm.register_update_system(std::make_unique<ParkRatingSystem>());
}

// ── Loans + finance history + bankruptcy guard ────────────────────────────
// Day-rollover sequence (continued): this system registers AFTER
// StaffPayrollSystem (see update_systems.cpp), so by the time it records
// the closing day the inspection grant and the payroll have already
// landed in total_earned / total_spent, and the crisis check sees cash
// after every rollover charge.

// Announce on both channels (toast + NEWS ticker queue), like events.
static void announce_finance(const std::string& text) {
    spawn_toast(text);
    auto* director = EntityHelper::get_singleton_cmp<EventDirectorState>();
    if (director) {
        if (director->announcement_queue.size() > 16)
            director->announcement_queue.erase(
                director->announcement_queue.begin());
        director->announcement_queue.push_back(text);
    }
}

// Borrow one of LOAN_AMOUNTS. Cash now, debt tracked. Refused (no
// change) for a non-loan amount or when debt + amount would pass the
// tier-scaled cap. Loan principal is not income: totals are untouched.
bool borrow_loan(int amount) {
    auto* econ = EntityHelper::get_singleton_cmp<Economy>();
    if (!econ) return false;
    if (!Economy::is_loan_amount(amount)) {
        spawn_toast(fmt::format(
            "No such loan: ${}. Loans are $5000 / $10000 / $25000.", amount));
        log_warn("Loan refused: ${} is not a loan amount", amount);
        return false;
    }
    int tier = 0;
    if (auto* lineup = EntityHelper::get_singleton_cmp<LineupState>())
        tier = lineup->tier;
    int cap = Economy::debt_cap_for_tier(tier);
    if (econ->debt + amount > cap) {
        spawn_toast(fmt::format(
            "Loan refused: debt would exceed the ${} cap (Tier {}).", cap,
            tier));
        log_warn("Loan refused: ${} would push debt ${} past cap ${}", amount,
                 econ->debt, cap);
        return false;
    }
    econ->cash += amount;
    econ->debt += amount;
    spawn_toast(fmt::format("Borrowed ${}. Debt now ${}.", amount, econ->debt));
    log_info("Borrowed ${}: cash ${}, debt ${}", amount, econ->cash,
             econ->debt);
    return true;
}

// Repay up to `amount` of the debt (capped at the debt). Refused (no
// change) with no debt, a non-positive amount, or unaffordable.
// Repayments count as expenses: they leave cash for good.
bool repay_loan(int amount) {
    auto* econ = EntityHelper::get_singleton_cmp<Economy>();
    if (!econ) return false;
    if (econ->debt <= 0) {
        spawn_toast("No debt to repay.");
        return false;
    }
    if (amount <= 0) {
        spawn_toast("Repay amount must be positive.");
        return false;
    }
    int pay = std::min(amount, econ->debt);
    if (econ->cash < pay) {
        spawn_toast(fmt::format("Not enough cash to repay ${} (cash ${}).", pay,
                                econ->cash));
        log_warn("Repay refused: ${} needed, cash ${}", pay, econ->cash);
        return false;
    }
    econ->cash -= pay;
    econ->debt -= pay;
    econ->total_spent += pay;
    spawn_toast(fmt::format("Repaid ${}. Debt now ${}.", pay, econ->debt));
    log_info("Repaid ${}: cash ${}, debt ${}", pay, econ->cash, econ->debt);
    return true;
}

// Repay everything the park can afford: min(debt, cash). If cash covers
// the whole debt this clears it; if not, it pays all available cash.
bool repay_loan_all() {
    auto* econ = EntityHelper::get_singleton_cmp<Economy>();
    if (!econ) return false;
    if (econ->debt <= 0) {
        spawn_toast("No debt to repay.");
        return false;
    }
    int pay = std::min(econ->debt, econ->cash);
    if (pay <= 0) {
        spawn_toast("No cash to repay the debt with.");
        return false;
    }
    econ->cash -= pay;
    econ->debt -= pay;
    econ->total_spent += pay;
    spawn_toast(fmt::format("Repaid ${}. Debt now ${}.", pay, econ->debt));
    log_info("Repaid all ${}: cash ${}, debt ${}", pay, econ->cash, econ->debt);
    return true;
}

// Deduct the day's wages unconditionally, without the payroll system's
// quit-out (staff quit when wages are unaffordable). E2E-only hook for
// the cash-crisis path: wages are the charge that drives cash negative,
// and the FinanceSystem guard then floors it at 0 with the rating hit.
void charge_wages_unchecked() {
    auto* econ = EntityHelper::get_singleton_cmp<Economy>();
    if (!econ) return;
    int wages = staff_total_wages();
    if (wages <= 0) return;
    econ->cash -= wages;
    econ->total_spent += wages;
    log_info("Wages charged (unchecked): ${}, cash now ${}", wages, econ->cash);
}

struct FinanceSystem : System<> {
    void once(float) override {
        if (skip_game_logic()) return;
        auto* econ = EntityHelper::get_singleton_cmp<Economy>();
        if (!econ) return;

        // Bankruptcy guard: cash floors at 0, rating takes the hit.
        // Every frame (not only rollover), so the floor lands the same
        // frame any charge pushes cash negative; after the floor,
        // cash == 0, so this cannot re-fire until cash goes negative
        // again. No game-over: the park keeps running, poorer.
        if (econ->cash < 0) {
            econ->cash = 0;
            if (auto* rating =
                    EntityHelper::get_singleton_cmp<ParkRatingState>()) {
                rating->rating =
                    std::clamp(rating->rating - CASH_CRISIS_RATING_PENALTY,
                               static_cast<float>(PARK_RATING_MIN),
                               static_cast<float>(PARK_RATING_MAX));
                rating->harm_pool =
                    std::min(PARK_RATING_HARM_CAP,
                             rating->harm_pool + CASH_CRISIS_RATING_PENALTY);
            }
            announce_finance(
                "CASH CRISIS! Cash fell below $0 — floored at $0, "
                "rating takes a hit.");
            log_warn("Cash crisis: cash floored at $0, rating hit");
        }

        // Day rollover: close the books on the day that just ended,
        // then compound interest onto the debt.
        auto* diff = EntityHelper::get_singleton_cmp<DifficultyState>();
        if (!diff || diff->day_number == econ->last_finance_day) return;
        int closed_day = econ->last_finance_day;
        int new_day = diff->day_number;
        econ->last_finance_day = new_day;
        if (new_day > closed_day) {
            FinanceDay row{closed_day,
                           econ->total_earned - econ->day_start_earned,
                           econ->total_spent - econ->day_start_spent};
            econ->finance_history.push_back(row);
            while (static_cast<int>(econ->finance_history.size()) >
                   FINANCE_HISTORY_MAX_DAYS)
                econ->finance_history.erase(econ->finance_history.begin());
            econ->day_start_earned = econ->total_earned;
            econ->day_start_spent = econ->total_spent;
            log_info("Finance history: Day {} income ${} expenses ${}", row.day,
                     row.income, row.expenses);
            if (econ->debt > 0) {
                int interest = Economy::loan_interest_for(econ->debt);
                econ->debt += interest;
                log_info("Loan interest: +${} on debt, now ${}", interest,
                         econ->debt);
            }
        } else {
            // Day went backwards (E2E set_day_number): resync the
            // snapshots so the next rollover does not double-count.
            econ->day_start_earned = econ->total_earned;
            econ->day_start_spent = econ->total_spent;
        }
    }
};

void register_finance_systems(SystemManager& sm) {
    sm.register_update_system(std::make_unique<FinanceSystem>());
}
