#pragma once

#include <filesystem>
#include <fstream>
#include <string>

#include "afterhours/src/core/entity_helper.h"
#include "afterhours/src/core/entity_query.h"
#include "components.h"
#include "game.h"

namespace save {

static constexpr const char* SAVE_DIR = "saves";
static constexpr const char* SAVE_FILE = "saves/game.sav";
static constexpr const char* META_FILE = "saves/meta.dat";
static constexpr uint32_t SAVE_MAGIC = 0xEDC10001;
// v2 appends economy, lineup, staff, GameState extras, inspection,
// director, and active events AFTER the v1 agent block, so the v1
// prefix is byte-identical. Increment on schema changes.
static constexpr uint32_t SAVE_VERSION = 2;

namespace detail {
inline void write_int(std::ofstream& f, int v) {
    f.write(reinterpret_cast<const char*>(&v), sizeof(int));
}
inline void write_float(std::ofstream& f, float v) {
    f.write(reinterpret_cast<const char*>(&v), sizeof(float));
}
inline void write_u8(std::ofstream& f, uint8_t v) {
    f.write(reinterpret_cast<const char*>(&v), 1);
}
inline void write_int_vec(std::ofstream& f, const std::vector<int>& vec) {
    write_int(f, static_cast<int>(vec.size()));
    for (int v : vec) write_int(f, v);
}
inline void write_string(std::ofstream& f, const std::string& s) {
    write_int(f, static_cast<int>(s.size()));
    f.write(s.data(), static_cast<std::streamsize>(s.size()));
}
inline void read_int(std::ifstream& f, int& v) {
    f.read(reinterpret_cast<char*>(&v), sizeof(int));
}
inline void read_float(std::ifstream& f, float& v) {
    f.read(reinterpret_cast<char*>(&v), sizeof(float));
}
inline void read_u8(std::ifstream& f, uint8_t& v) {
    f.read(reinterpret_cast<char*>(&v), 1);
}
inline void read_int_vec(std::ifstream& f, std::vector<int>& vec) {
    int n = 0;
    read_int(f, n);
    vec.clear();
    for (int i = 0; i < n; i++) {
        int v = 0;
        read_int(f, v);
        vec.push_back(v);
    }
}
inline void read_string(std::ifstream& f, std::string& s) {
    int n = 0;
    read_int(f, n);
    s.assign(static_cast<size_t>(std::max(n, 0)), '\0');
    if (n > 0) f.read(s.data(), n);
}
}  // namespace detail

// Meta-progression: persists across sessions
struct MetaProgress {
    int best_day = 0;
    int best_agents_served = 0;
    int best_max_attendees = 0;
    float best_time_survived = 0.f;
    int total_runs = 0;
    int total_deaths = 0;
};

inline bool save_meta(const MetaProgress& meta) {
    std::filesystem::create_directories(SAVE_DIR);
    std::ofstream f(META_FILE, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(&SAVE_MAGIC), sizeof(SAVE_MAGIC));
    f.write(reinterpret_cast<const char*>(&meta), sizeof(MetaProgress));
    return f.good();
}

inline bool load_meta(MetaProgress& meta) {
    std::ifstream f(META_FILE, std::ios::binary);
    if (!f) return false;
    uint32_t magic = 0;
    f.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    if (magic != SAVE_MAGIC) return false;
    f.read(reinterpret_cast<char*>(&meta), sizeof(MetaProgress));
    return f.good();
}

// Save game state to binary file
inline bool save_game() {
    std::filesystem::create_directories(SAVE_DIR);
    std::ofstream f(SAVE_FILE, std::ios::binary);
    if (!f) return false;

    f.write(reinterpret_cast<const char*>(&SAVE_MAGIC), sizeof(SAVE_MAGIC));
    f.write(reinterpret_cast<const char*>(&SAVE_VERSION), sizeof(SAVE_VERSION));

    // Grid tiles
    auto* grid = afterhours::EntityHelper::get_singleton_cmp<Grid>();
    if (!grid) return false;
    for (int z = 0; z < MAP_SIZE; z++) {
        for (int x = 0; x < MAP_SIZE; x++) {
            auto& tile = grid->at(x, z);
            auto type = static_cast<uint8_t>(tile.type);
            f.write(reinterpret_cast<const char*>(&type), 1);
            f.write(reinterpret_cast<const char*>(&tile.agent_count),
                    sizeof(int));
            f.write(reinterpret_cast<const char*>(tile.pheromone.data()), 5);
        }
    }

    // Game state
    auto* gs = afterhours::EntityHelper::get_singleton_cmp<GameState>();
    if (gs) {
        auto status = static_cast<uint8_t>(gs->status);
        f.write(reinterpret_cast<const char*>(&status), 1);
        f.write(reinterpret_cast<const char*>(&gs->game_time), sizeof(float));
        f.write(reinterpret_cast<const char*>(&gs->death_count), sizeof(int));
        f.write(reinterpret_cast<const char*>(&gs->total_agents_served),
                sizeof(int));
        f.write(reinterpret_cast<const char*>(&gs->time_survived),
                sizeof(float));
        f.write(reinterpret_cast<const char*>(&gs->max_attendees), sizeof(int));
        f.write(reinterpret_cast<const char*>(&gs->agents_exited), sizeof(int));
        f.write(reinterpret_cast<const char*>(&gs->carryover_count),
                sizeof(int));
        f.write(reinterpret_cast<const char*>(&gs->speed_multiplier),
                sizeof(float));
    }

    // Game clock
    auto* clock = afterhours::EntityHelper::get_singleton_cmp<GameClock>();
    if (clock) {
        f.write(reinterpret_cast<const char*>(&clock->game_time_minutes),
                sizeof(float));
        auto spd = static_cast<uint8_t>(clock->speed);
        f.write(reinterpret_cast<const char*>(&spd), 1);
    }

    // Difficulty
    auto* diff = afterhours::EntityHelper::get_singleton_cmp<DifficultyState>();
    if (diff) {
        f.write(reinterpret_cast<const char*>(&diff->day_number), sizeof(int));
        f.write(reinterpret_cast<const char*>(&diff->spawn_rate_mult),
                sizeof(float));
        f.write(reinterpret_cast<const char*>(&diff->crowd_size_mult),
                sizeof(float));
    }

    // Agent count + positions
    auto agents =
        afterhours::EntityQuery().whereHasComponent<Agent, Transform>().gen();
    int agent_count = static_cast<int>(agents.size());
    f.write(reinterpret_cast<const char*>(&agent_count), sizeof(int));
    for (afterhours::Entity& a_entity : agents) {
        auto& agent = a_entity.get<Agent>();
        auto& tf = a_entity.get<Transform>();
        auto want = static_cast<uint8_t>(agent.want);
        f.write(reinterpret_cast<const char*>(&want), 1);
        f.write(reinterpret_cast<const char*>(&tf.position.x), sizeof(float));
        f.write(reinterpret_cast<const char*>(&tf.position.y), sizeof(float));
        f.write(reinterpret_cast<const char*>(&agent.target_grid_x),
                sizeof(int));
        f.write(reinterpret_cast<const char*>(&agent.target_grid_z),
                sizeof(int));
        f.write(reinterpret_cast<const char*>(&agent.color_idx), 1);

        float hp = 1.0f;
        if (!a_entity.is_missing<AgentHealth>()) {
            hp = a_entity.get<AgentHealth>().hp;
        }
        f.write(reinterpret_cast<const char*>(&hp), sizeof(float));
    }

    // ── v2 sections (appended after the v1 agent block) ─────────────
    // Economy
    {
        auto* econ = afterhours::EntityHelper::get_singleton_cmp<Economy>();
        detail::write_int(f, econ ? econ->cash : STARTING_CASH);
        detail::write_int(f, econ ? econ->ticket_price : DEFAULT_TICKET_PRICE);
        detail::write_int(f, econ ? econ->total_earned : 0);
        detail::write_int(f, econ ? econ->total_spent : 0);
    }
    // Lineup: bookings + active acts + tier progress
    {
        auto* lineup =
            afterhours::EntityHelper::get_singleton_cmp<LineupState>();
        detail::write_int(f, lineup ? lineup->tier : 0);
        detail::write_int(f, lineup ? lineup->last_rollover_day : 1);
        detail::write_int_vec(f, lineup ? lineup->booked : std::vector<int>{});
        detail::write_int_vec(f, lineup ? lineup->active : std::vector<int>{});
    }
    // Staff payroll bookkeeping
    {
        auto* staff_state =
            afterhours::EntityHelper::get_singleton_cmp<StaffState>();
        detail::write_int(f, staff_state ? staff_state->last_payroll_day : 1);
        detail::write_int(f, staff_state ? staff_state->quit_count : 0);
    }
    // Staff roster: one entry per StaffMember entity
    {
        auto staff =
            afterhours::EntityQuery().whereHasComponent<StaffMember>().gen();
        detail::write_int(f, static_cast<int>(staff.size()));
        for (afterhours::Entity& s_entity : staff) {
            auto& s = s_entity.get<StaffMember>();
            detail::write_u8(f, static_cast<uint8_t>(s.role));
            detail::write_int(f, s.home_x);
            detail::write_int(f, s.home_z);
            float px = 0.f, pz = 0.f;
            if (!s_entity.is_missing<Transform>()) {
                px = s_entity.get<Transform>().position.x;
                pz = s_entity.get<Transform>().position.y;
            }
            detail::write_float(f, px);
            detail::write_float(f, pz);
            detail::write_int(f, s.target_x);
            detail::write_int(f, s.target_z);
            detail::write_int(f, s.move_x);
            detail::write_int(f, s.move_z);
        }
    }
    // GameState extras: tier progress (total_arrivals) + inspection inputs
    {
        detail::write_int(f, gs ? gs->total_arrivals : 0);
        detail::write_int(f, gs ? gs->milestone_idx : 0);
        detail::write_int(f, gs ? gs->injury_count : 0);
    }
    // Inspection state (so a load does not re-grant or double-count)
    {
        auto* insp =
            afterhours::EntityHelper::get_singleton_cmp<InspectionState>();
        detail::write_u8(f,
                         insp ? static_cast<uint8_t>(insp->last_grade) : '-');
        detail::write_int(f, insp ? insp->last_score : -1);
        detail::write_int(f, insp ? insp->inspections_done : 0);
        detail::write_int(f, insp ? insp->consecutive_bad : 0);
        detail::write_int(f, insp ? insp->density_violations : 0);
        detail::write_int(f, insp ? insp->deaths_at_last : 0);
        detail::write_int(f, insp ? insp->injuries_at_last : 0);
        detail::write_int(f, insp ? insp->last_inspected_day : 1);
        detail::write_u8(f, insp && insp->shutdown_triggered ? 1 : 0);
    }
    // Event director bookkeeping (no-repeat sets + quota). The
    // announcement queue is transient UI and is not persisted.
    {
        auto* director =
            afterhours::EntityHelper::get_singleton_cmp<EventDirectorState>();
        detail::write_int(f, director ? director->last_day : 1);
        detail::write_int(f, director ? director->events_fired_today : 0);
        detail::write_int(f, director ? director->events_target_today : 0);
        detail::write_float(f, director ? director->next_fire_in : 0.f);
        detail::write_int_vec(
            f, director ? director->fired_today : std::vector<int>{});
        detail::write_int_vec(
            f, director ? director->fired_yesterday : std::vector<int>{});
        detail::write_string(f, director ? director->seed : "default_seed");
    }
    // Active events (timed only; instant events leave no entity)
    {
        auto events =
            afterhours::EntityQuery().whereHasComponent<ActiveEvent>().gen();
        detail::write_int(f, static_cast<int>(events.size()));
        for (afterhours::Entity& ev_entity : events) {
            auto& ev = ev_entity.get<ActiveEvent>();
            detail::write_int(f, static_cast<int>(ev.type));
            detail::write_float(f, ev.duration);
            detail::write_float(f, ev.elapsed);
            detail::write_string(f, ev.description);
            detail::write_int(f, static_cast<int>(ev.offline_type));
        }
    }

    return f.good();
}

// Load game state from binary file
inline bool load_game() {
    std::ifstream f(SAVE_FILE, std::ios::binary);
    if (!f) return false;

    uint32_t magic = 0;
    f.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    if (magic != SAVE_MAGIC) return false;

    uint32_t version = 0;
    f.read(reinterpret_cast<char*>(&version), sizeof(version));
    // v1 files are a byte-identical prefix of v2: they load, with the
    // v2-only state (economy, bookings, staff, ...) left as-is.
    // Anything else is rejected.
    if (version != SAVE_VERSION && version != 1) return false;

    // Grid tiles
    auto* grid = afterhours::EntityHelper::get_singleton_cmp<Grid>();
    if (!grid) return false;
    for (int z = 0; z < MAP_SIZE; z++) {
        for (int x = 0; x < MAP_SIZE; x++) {
            auto& tile = grid->at(x, z);
            uint8_t type;
            f.read(reinterpret_cast<char*>(&type), 1);
            tile.type = static_cast<TileType>(type);
            f.read(reinterpret_cast<char*>(&tile.agent_count), sizeof(int));
            f.read(reinterpret_cast<char*>(tile.pheromone.data()), 5);
        }
    }

    // Game state
    auto* gs = afterhours::EntityHelper::get_singleton_cmp<GameState>();
    if (gs) {
        uint8_t status;
        f.read(reinterpret_cast<char*>(&status), 1);
        gs->status = static_cast<GameStatus>(status);
        f.read(reinterpret_cast<char*>(&gs->game_time), sizeof(float));
        f.read(reinterpret_cast<char*>(&gs->death_count), sizeof(int));
        f.read(reinterpret_cast<char*>(&gs->total_agents_served), sizeof(int));
        f.read(reinterpret_cast<char*>(&gs->time_survived), sizeof(float));
        f.read(reinterpret_cast<char*>(&gs->max_attendees), sizeof(int));
        f.read(reinterpret_cast<char*>(&gs->agents_exited), sizeof(int));
        f.read(reinterpret_cast<char*>(&gs->carryover_count), sizeof(int));
        f.read(reinterpret_cast<char*>(&gs->speed_multiplier), sizeof(float));
    }

    // Game clock
    auto* clock = afterhours::EntityHelper::get_singleton_cmp<GameClock>();
    if (clock) {
        f.read(reinterpret_cast<char*>(&clock->game_time_minutes),
               sizeof(float));
        uint8_t spd;
        f.read(reinterpret_cast<char*>(&spd), 1);
        clock->speed = static_cast<GameSpeed>(spd);
    }

    // Difficulty
    auto* diff = afterhours::EntityHelper::get_singleton_cmp<DifficultyState>();
    if (diff) {
        f.read(reinterpret_cast<char*>(&diff->day_number), sizeof(int));
        f.read(reinterpret_cast<char*>(&diff->spawn_rate_mult), sizeof(float));
        f.read(reinterpret_cast<char*>(&diff->crowd_size_mult), sizeof(float));
    }

    // Clean up existing agents
    auto old_agents =
        afterhours::EntityQuery().whereHasComponent<Agent>().gen();
    for (afterhours::Entity& a : old_agents) a.cleanup = true;
    afterhours::EntityHelper::cleanup();

    // Load agents
    int agent_count = 0;
    f.read(reinterpret_cast<char*>(&agent_count), sizeof(int));
    for (int i = 0; i < agent_count; i++) {
        uint8_t want;
        float px, pz;
        int tx, tz;
        uint8_t color_idx;
        float hp;
        f.read(reinterpret_cast<char*>(&want), 1);
        f.read(reinterpret_cast<char*>(&px), sizeof(float));
        f.read(reinterpret_cast<char*>(&pz), sizeof(float));
        f.read(reinterpret_cast<char*>(&tx), sizeof(int));
        f.read(reinterpret_cast<char*>(&tz), sizeof(int));
        f.read(reinterpret_cast<char*>(&color_idx), 1);
        f.read(reinterpret_cast<char*>(&hp), sizeof(float));

        auto& e = afterhours::EntityHelper::createEntity();
        e.addComponent<Transform>(::vec2{px, pz});
        e.addComponent<Agent>(static_cast<FacilityType>(want), tx, tz);
        e.get<Agent>().color_idx = color_idx;
        e.addComponent<AgentHealth>();
        e.get<AgentHealth>().hp = hp;
        e.addComponent<AgentNeeds>();
    }
    // Merge before the v2 blocks: they query (staff, active events),
    // and a query with unmerged temp entities trips EntityQuery's
    // assert.
    afterhours::EntityHelper::merge_entity_arrays();

    // ── v2 sections (absent in v1 files: leave v2-only state as-is) ──
    // Reads are unconditional into locals (only the assignment is
    // guarded) so a missing singleton can never desync the stream.
    if (version == SAVE_VERSION) {
        // Economy
        {
            int cash = 0, price = 0, earned = 0, spent = 0;
            detail::read_int(f, cash);
            detail::read_int(f, price);
            detail::read_int(f, earned);
            detail::read_int(f, spent);
            if (auto* econ =
                    afterhours::EntityHelper::get_singleton_cmp<Economy>()) {
                econ->cash = cash;
                econ->ticket_price = price;
                econ->total_earned = earned;
                econ->total_spent = spent;
            }
        }
        // Lineup: bookings + active acts + tier progress
        {
            int tier = 0, rollover = 1;
            std::vector<int> booked, active;
            detail::read_int(f, tier);
            detail::read_int(f, rollover);
            detail::read_int_vec(f, booked);
            detail::read_int_vec(f, active);
            if (auto* lineup = afterhours::EntityHelper::get_singleton_cmp<
                    LineupState>()) {
                lineup->tier = tier;
                lineup->last_rollover_day = rollover;
                lineup->booked = booked;
                lineup->active = active;
            }
        }
        // Staff payroll bookkeeping
        {
            int payroll_day = 1, quits = 0;
            detail::read_int(f, payroll_day);
            detail::read_int(f, quits);
            if (auto* staff_state =
                    afterhours::EntityHelper::get_singleton_cmp<StaffState>()) {
                staff_state->last_payroll_day = payroll_day;
                staff_state->quit_count = quits;
            }
        }
        // Staff roster: replace the current roster with the saved one
        {
            auto old_staff = afterhours::EntityQuery()
                                 .whereHasComponent<StaffMember>()
                                 .gen();
            for (afterhours::Entity& s : old_staff) s.cleanup = true;
            afterhours::EntityHelper::cleanup();

            int staff_count = 0;
            detail::read_int(f, staff_count);
            for (int i = 0; i < staff_count; i++) {
                uint8_t role_u8 = 0;
                int hx = 0, hz = 0, tx = -1, tz = -1, mx = -1, mz = -1;
                float px = 0.f, pz = 0.f;
                detail::read_u8(f, role_u8);
                detail::read_int(f, hx);
                detail::read_int(f, hz);
                detail::read_float(f, px);
                detail::read_float(f, pz);
                detail::read_int(f, tx);
                detail::read_int(f, tz);
                detail::read_int(f, mx);
                detail::read_int(f, mz);
                auto& e = afterhours::EntityHelper::createEntity();
                e.addComponent<Transform>(::vec2{px, pz});
                e.addComponent<StaffMember>();
                auto& s = e.get<StaffMember>();
                s.role = static_cast<StaffRole>(role_u8);
                s.home_x = hx;
                s.home_z = hz;
                s.target_x = tx;
                s.target_z = tz;
                s.move_x = mx;
                s.move_z = mz;
            }
            // Merge before the active-events block queries: same
            // unmerged-temp assert as above.
            afterhours::EntityHelper::merge_entity_arrays();
        }
        // GameState extras
        {
            int arrivals = 0, milestone = 0, injuries = 0;
            detail::read_int(f, arrivals);
            detail::read_int(f, milestone);
            detail::read_int(f, injuries);
            if (gs) {
                gs->total_arrivals = arrivals;
                gs->milestone_idx = milestone;
                gs->injury_count = injuries;
            }
        }
        // Inspection state
        {
            uint8_t grade = 0, shutdown = 0;
            int score = -1, done = 0, bad = 0, violations = 0, deaths = 0,
                injuries = 0, inspected_day = 1;
            detail::read_u8(f, grade);
            detail::read_int(f, score);
            detail::read_int(f, done);
            detail::read_int(f, bad);
            detail::read_int(f, violations);
            detail::read_int(f, deaths);
            detail::read_int(f, injuries);
            detail::read_int(f, inspected_day);
            detail::read_u8(f, shutdown);
            if (auto* insp = afterhours::EntityHelper::get_singleton_cmp<
                    InspectionState>()) {
                insp->last_grade = static_cast<char>(grade);
                insp->last_score = score;
                insp->inspections_done = done;
                insp->consecutive_bad = bad;
                insp->density_violations = violations;
                insp->deaths_at_last = deaths;
                insp->injuries_at_last = injuries;
                insp->last_inspected_day = inspected_day;
                insp->shutdown_triggered = shutdown != 0;
            }
        }
        // Event director bookkeeping
        {
            int last_day = 1, fired = 0, target = 0;
            float next_fire = 0.f;
            std::vector<int> today, yesterday;
            std::string seed;
            detail::read_int(f, last_day);
            detail::read_int(f, fired);
            detail::read_int(f, target);
            detail::read_float(f, next_fire);
            detail::read_int_vec(f, today);
            detail::read_int_vec(f, yesterday);
            detail::read_string(f, seed);
            if (auto* director = afterhours::EntityHelper::get_singleton_cmp<
                    EventDirectorState>()) {
                director->last_day = last_day;
                director->events_fired_today = fired;
                director->events_target_today = target;
                director->next_fire_in = next_fire;
                director->fired_today = today;
                director->fired_yesterday = yesterday;
                director->seed = seed;
                director->initialized = true;
                director->announcement_queue.clear();
            }
        }
        // Active events: replace the current ones with the saved ones
        {
            auto old_events = afterhours::EntityQuery()
                                  .whereHasComponent<ActiveEvent>()
                                  .gen();
            for (afterhours::Entity& e : old_events) e.cleanup = true;
            afterhours::EntityHelper::cleanup();

            int event_count = 0;
            detail::read_int(f, event_count);
            for (int i = 0; i < event_count; i++) {
                int type_i = 0, offline_i = 0;
                float duration = 0.f, elapsed = 0.f;
                std::string description;
                detail::read_int(f, type_i);
                detail::read_float(f, duration);
                detail::read_float(f, elapsed);
                detail::read_string(f, description);
                detail::read_int(f, offline_i);
                auto& e = afterhours::EntityHelper::createEntity();
                e.addComponent<ActiveEvent>();
                auto& ev = e.get<ActiveEvent>();
                ev.type = static_cast<EventType>(type_i);
                ev.duration = duration;
                ev.elapsed = elapsed;
                ev.description = description;
                ev.offline_type = static_cast<FacilityType>(offline_i);
            }
        }
    }

    afterhours::EntityHelper::merge_entity_arrays();
    grid->mark_tiles_dirty();
    return f.good();
}

// Update meta-progression with current run stats
inline void update_meta_on_game_over() {
    MetaProgress meta;
    load_meta(meta);

    auto* gs = afterhours::EntityHelper::get_singleton_cmp<GameState>();
    auto* diff = afterhours::EntityHelper::get_singleton_cmp<DifficultyState>();
    if (!gs) return;

    meta.total_runs++;
    meta.total_deaths += gs->death_count;

    if (diff && diff->day_number > meta.best_day) {
        meta.best_day = diff->day_number;
    }
    if (gs->total_agents_served > meta.best_agents_served) {
        meta.best_agents_served = gs->total_agents_served;
    }
    if (gs->max_attendees > meta.best_max_attendees) {
        meta.best_max_attendees = gs->max_attendees;
    }
    if (gs->time_survived > meta.best_time_survived) {
        meta.best_time_survived = gs->time_survived;
    }

    save_meta(meta);
}

// Check if a save file exists
inline bool has_save_file() { return std::filesystem::exists(SAVE_FILE); }

// Delete save file
inline void delete_save() {
    if (std::filesystem::exists(SAVE_FILE)) {
        std::filesystem::remove(SAVE_FILE);
    }
}

}  // namespace save
