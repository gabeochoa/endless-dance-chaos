// Staff domain: hireable Guard / Medic / Janitor entities.
// Guards patrol around their hire tile (mobile crush-damage aura),
// medics walk to the nearest collapsed agent, janitors roam and cut
// facility service times park-wide. Payroll runs on the day rollover:
// if cash cannot cover the remaining wages, the highest-paid staff quit
// one by one (ticker announcement) until it can.
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "afterhours/src/core/entity_query.h"
#include "components.h"
#include "engine/random_engine.h"
#include "systems.h"
#include "update_helpers.h"

int staff_count(StaffRole role) {
    int n = 0;
    auto staff = EntityQuery().whereHasComponent<StaffMember>().gen();
    for (Entity& e : staff)
        if (e.get<StaffMember>().role == role) n++;
    return n;
}

int staff_total_wages() {
    int total = 0;
    auto staff = EntityQuery().whereHasComponent<StaffMember>().gen();
    for (Entity& e : staff) total += staff_wage_for(e.get<StaffMember>().role);
    return total;
}

float staff_service_time_factor() {
    float f = 1.0f - STAFF_JANITOR_SERVICE_MULT *
                         static_cast<float>(staff_count(StaffRole::Janitor));
    return std::max(f, 0.5f);
}

static bool staff_near(int gx, int gz, StaffRole role, float radius) {
    auto* grid = EntityHelper::get_singleton_cmp<Grid>();
    if (!grid) return false;
    float r2 = radius * radius;
    auto staff =
        EntityQuery().whereHasComponent<StaffMember, Transform>().gen();
    for (Entity& e : staff) {
        if (e.get<StaffMember>().role != role) continue;
        auto& tf = e.get<Transform>();
        auto [sx, sz] = grid->world_to_grid(tf.position.x, tf.position.y);
        float dx = static_cast<float>(sx - gx);
        float dz = static_cast<float>(sz - gz);
        if (dx * dx + dz * dz <= r2) return true;
    }
    return false;
}

bool guard_near(int gx, int gz) {
    return staff_near(gx, gz, StaffRole::Guard, STAFF_GUARD_RADIUS);
}

bool medic_near(int gx, int gz) {
    return staff_near(gx, gz, StaffRole::Medic, STAFF_MEDIC_RADIUS);
}

bool hire_staff(StaffRole role, int x, int z) {
    auto* econ = EntityHelper::get_singleton_cmp<Economy>();
    auto* grid = EntityHelper::get_singleton_cmp<Grid>();
    if (!econ || !grid) return false;
    int bonus = staff_bonus_for(role);
    if (!econ->try_spend(bonus)) {
        spawn_toast(fmt::format("Not enough cash! {} signing bonus is ${}",
                                staff_name_for(role), bonus));
        log_warn("Hire refused: {} bonus ${}, cash ${}", staff_name_for(role),
                 bonus, econ->cash);
        return false;
    }
    int hx = std::clamp(x, PLAY_MIN, grid->play_max_x());
    int hz = std::clamp(z, PLAY_MIN, grid->play_max_z());
    Entity& e = EntityHelper::createEntity();
    e.addComponent<Transform>(grid->grid_to_world(hx, hz));
    e.addComponent<StaffMember>();
    auto& s = e.get<StaffMember>();
    s.role = role;
    s.home_x = hx;
    s.home_z = hz;
    EntityHelper::merge_entity_arrays();
    spawn_toast(
        fmt::format("Hired a {}! Signing bonus ${}, wage ${}/day. "
                    "Please do not ask about the contract.",
                    staff_name_for(role), bonus, staff_wage_for(role)));
    log_info("Hired {} at ({},{}) bonus ${}", staff_name_for(role), hx, hz,
             bonus);
    return true;
}

bool fire_staff(StaffRole role) {
    auto staff = EntityQuery().whereHasComponent<StaffMember>().gen();
    for (Entity& e : staff) {
        if (e.get<StaffMember>().role == role) {
            e.cleanup = true;
            EntityHelper::cleanup();
            spawn_toast(
                fmt::format("Fired the {}. No severance, no refund, "
                            "no regrets (management's words).",
                            staff_name_for(role)));
            log_info("Fired {}", staff_name_for(role));
            return true;
        }
    }
    return false;
}

// Wages are due once per day rollover. While cash cannot cover the
// payroll, the highest-paid remaining staff quit (quit_count feeds the
// ticker); the survivors are then paid in full.
static void run_payroll() {
    auto* econ = EntityHelper::get_singleton_cmp<Economy>();
    auto* staff_state = EntityHelper::get_singleton_cmp<StaffState>();
    if (!econ || !staff_state) return;
    int wages = staff_total_wages();
    if (wages <= 0) return;
    while (wages > econ->cash) {
        // Quit the highest-paid staffer first (survivors stay affordable).
        auto staff = EntityQuery().whereHasComponent<StaffMember>().gen();
        Entity* victim = nullptr;
        int victim_wage = -1;
        for (Entity& e : staff) {
            int w = staff_wage_for(e.get<StaffMember>().role);
            if (w > victim_wage) {
                victim_wage = w;
                victim = &e;
            }
        }
        if (!victim) break;
        auto role = victim->get<StaffMember>().role;
        victim->cleanup = true;
        staff_state->quit_count++;
        wages -= victim_wage;
        log_info("Staff quit: {} (unpaid wages ${}, cash ${})",
                 staff_name_for(role), victim_wage, econ->cash);
        // Clean up now, not after the loop: the next query must not
        // re-pick this same flagged entity as the next victim.
        EntityHelper::cleanup();
    }
    if (wages > 0) {
        econ->cash -= wages;
        econ->total_spent += wages;
        log_info("Staff payroll: wages ${}, cash now ${}", wages, econ->cash);
    }
}

struct StaffPayrollSystem : System<> {
    void once(float) override {
        if (skip_game_logic()) return;
        auto* diff = EntityHelper::get_singleton_cmp<DifficultyState>();
        auto* staff_state = EntityHelper::get_singleton_cmp<StaffState>();
        if (!diff || !staff_state) return;
        if (diff->day_number != staff_state->last_payroll_day) {
            int new_day = diff->day_number;
            staff_state->last_payroll_day = new_day;
            if (new_day > 1) run_payroll();
        }
    }
};

// Greedy one-step pathfinding for staff: same neighbor-scoring shape as
// AgentMovementSystem (Manhattan distance, blocked tiles excluded), but
// staff ignore density/pheromones and never get stuck behind a crowd.
static std::pair<int, int> staff_next_tile(int cur_x, int cur_z, int goal_x,
                                           int goal_z, const Grid& grid) {
    static constexpr int dx[] = {1, -1, 0, 0};
    static constexpr int dz[] = {0, 0, 1, -1};
    auto& rng = RandomEngine::get();
    int order[4] = {0, 1, 2, 3};
    for (int k = 3; k > 0; k--) {
        int j = rng.get_int(0, k);
        std::swap(order[k], order[j]);
    }
    int best_x = cur_x, best_z = cur_z;
    int best_dist = std::abs(cur_x - goal_x) + std::abs(cur_z - goal_z);
    int walkable_x = cur_x, walkable_z = cur_z;
    bool have_walkable = false;
    for (int idx = 0; idx < 4; idx++) {
        int i = order[idx];
        int nx = cur_x + dx[i], nz = cur_z + dz[i];
        if (!grid.in_bounds(nx, nz)) continue;
        if (tile_blocks_movement(grid.at(nx, nz).type)) continue;
        if (!have_walkable) {
            walkable_x = nx;
            walkable_z = nz;
            have_walkable = true;
        }
        int dist = std::abs(nx - goal_x) + std::abs(nz - goal_z);
        if (dist < best_dist) {
            best_dist = dist;
            best_x = nx;
            best_z = nz;
        }
    }
    if (best_x == cur_x && best_z == cur_z && have_walkable) {
        // Lateral step around an obstacle instead of standing still.
        return {walkable_x, walkable_z};
    }
    return {best_x, best_z};
}

struct StaffMovementSystem : System<StaffMember, Transform> {
    void for_each_with(Entity& e, StaffMember& s, Transform& tf,
                       float dt) override {
        if (skip_game_logic()) return;
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;

        auto [cur_x, cur_z] = grid->world_to_grid(tf.position.x, tf.position.y);

        // Medics seek the nearest collapsed agent; everyone else patrols
        // around their hire tile.
        if (s.role == StaffRole::Medic) {
            auto collapsed =
                EntityQuery().whereHasComponent<Collapsed, Transform>().gen();
            int best_x = -1, best_z = -1, best_dist = INT_MAX;
            for (Entity& c : collapsed) {
                auto& ctf = c.get<Transform>();
                auto [cx, cz] =
                    grid->world_to_grid(ctf.position.x, ctf.position.y);
                int dist = std::abs(cx - cur_x) + std::abs(cz - cur_z);
                if (dist < best_dist) {
                    best_dist = dist;
                    best_x = cx;
                    best_z = cz;
                }
            }
            if (best_x >= 0) {
                if (s.target_x != best_x || s.target_z != best_z) {
                    s.target_x = best_x;
                    s.target_z = best_z;
                    s.move_x = -1;
                    s.move_z = -1;
                }
            } else if (s.target_x < 0 ||
                       (cur_x == s.target_x && cur_z == s.target_z)) {
                s.target_x = -1;
                s.target_z = -1;
            }
        }
        if (s.target_x < 0) {
            // New patrol target within STAFF_PATROL_RADIUS of home.
            auto& rng = RandomEngine::get();
            int r = static_cast<int>(STAFF_PATROL_RADIUS);
            int tx = std::clamp(s.home_x + rng.get_int(-r, r), PLAY_MIN,
                                grid->play_max_x());
            int tz = std::clamp(s.home_z + rng.get_int(-r, r), PLAY_MIN,
                                grid->play_max_z());
            if (grid->in_bounds(tx, tz) &&
                !tile_blocks_movement(grid->at(tx, tz).type)) {
                s.target_x = tx;
                s.target_z = tz;
                s.move_x = -1;
                s.move_z = -1;
            } else {
                return;
            }
        }
        if (cur_x == s.target_x && cur_z == s.target_z) {
            s.target_x = -1;
            s.target_z = -1;
            return;
        }
        bool need_step =
            s.move_x < 0 || (cur_x == s.move_x && cur_z == s.move_z);
        if (need_step) {
            auto [nx, nz] =
                staff_next_tile(cur_x, cur_z, s.target_x, s.target_z, *grid);
            s.move_x = nx;
            s.move_z = nz;
        }
        if (s.move_x == cur_x && s.move_z == cur_z) return;  // boxed in
        ::vec2 target_world = grid->grid_to_world(s.move_x, s.move_z);
        float dx = target_world.x - tf.position.x;
        float dz = target_world.y - tf.position.y;
        float dist = std::sqrt(dx * dx + dz * dz);
        if (dist > 0.01f) {
            float step = STAFF_SPEED * TILESIZE * dt;
            if (step > dist) step = dist;
            tf.position.x += (dx / dist) * step;
            tf.position.y += (dz / dist) * step;
        }
        (void) e;
    }
};

void register_staff_systems(SystemManager& sm) {
    sm.register_update_system(std::make_unique<StaffPayrollSystem>());
    sm.register_update_system(std::make_unique<StaffMovementSystem>());
}
