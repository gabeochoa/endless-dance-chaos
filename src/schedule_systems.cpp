// Schedule domain: artist schedule, agent spawning, difficulty scaling.
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "afterhours/src/core/entity_query.h"
#include "components.h"
#include "engine/random_engine.h"
#include "entity_makers.h"
#include "systems.h"
#include "update_helpers.h"

// Generate a scheduled artist at a given start time
static ScheduledArtist generate_artist(float start_minutes, int max_att) {
    auto& rng = RandomEngine::get();
    ScheduledArtist a;
    a.name = fmt::format("Artist {:03d}", rng.get_int(100, 999));
    a.start_time_minutes = start_minutes;
    a.duration_minutes = rng.get_float(30.f, 60.f);
    int base = 50 + (max_att / 10);
    int variation = static_cast<int>(base * 0.3f);
    a.expected_crowd = base + rng.get_int(-variation, variation);
    if (a.expected_crowd < 20) a.expected_crowd = 20;
    return a;
}

// Fill schedule with look-ahead artists starting from given time
static void fill_schedule(ArtistSchedule& sched, float after_time,
                          int max_att) {
    static constexpr float slots[] = {600, 720, 840, 960, 1080, 1200, 1320};
    static constexpr int NUM_SLOTS = 7;

    while ((int) sched.schedule.size() < sched.look_ahead) {
        float last_end = after_time;
        if (!sched.schedule.empty()) {
            auto& back = sched.schedule.back();
            last_end = back.start_time_minutes + back.duration_minutes + 30.f;
        }
        float next_slot = -1;
        for (int i = 0; i < NUM_SLOTS; i++) {
            if (slots[i] >= last_end) {
                next_slot = slots[i];
                break;
            }
        }
        if (next_slot < 0) {
            next_slot = slots[0] + 1440.f;
        }
        sched.schedule.push_back(generate_artist(next_slot, max_att));
    }
}

// Update artist schedule: advance states, manage stage state machine
struct UpdateArtistScheduleSystem : System<> {
    bool initialized = false;

    void once(float) override {
        if (skip_game_logic()) return;
        auto* sched = EntityHelper::get_singleton_cmp<ArtistSchedule>();
        auto* clock = EntityHelper::get_singleton_cmp<GameClock>();
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        if (!sched || !clock || !gs) return;

        float now = clock->game_time_minutes;

        if (!initialized) {
            fill_schedule(*sched, now, gs->max_attendees);
            initialized = true;
        }

        sched->stage_state = StageState::Idle;
        sched->current_artist_idx = -1;

        for (int i = 0; i < (int) sched->schedule.size(); i++) {
            auto& a = sched->schedule[i];
            if (a.finished) continue;

            float end_time = a.start_time_minutes + a.duration_minutes;
            float announce_time = a.start_time_minutes - 15.f;

            if (now >= end_time) {
                if (a.performing) {
                    a.performing = false;
                    a.finished = true;
                    sched->stage_state = StageState::Clearing;
                    log_info("Artist '{}' finished", a.name);
                }
            } else if (now >= a.start_time_minutes) {
                if (!a.performing) {
                    a.performing = true;
                    a.announced = true;
                    log_info("Artist '{}' now performing (crowd ~{})", a.name,
                             a.expected_crowd);
                }
                sched->stage_state = StageState::Performing;
                sched->current_artist_idx = i;
                break;
            } else if (now >= announce_time) {
                if (!a.announced) {
                    a.announced = true;
                    log_info("Announcing: '{}' at {}", a.name,
                             fmt::format("{:02d}:{:02d}",
                                         (int) (a.start_time_minutes / 60) % 24,
                                         (int) a.start_time_minutes % 60));
                }
                sched->stage_state = StageState::Announcing;
                break;
            } else {
                break;
            }
        }

        while (!sched->schedule.empty() && sched->schedule.front().finished) {
            sched->schedule.erase(sched->schedule.begin());
            if (sched->current_artist_idx > 0) sched->current_artist_idx--;
        }
        if ((int) sched->schedule.size() < sched->look_ahead) {
            fill_schedule(*sched, now, gs->max_attendees);
        }

        auto* ss = EntityHelper::get_singleton_cmp<SpawnState>();
        if (ss && !ss->manual_override) {
            float base_rate = 1.f / DEFAULT_SPAWN_INTERVAL;
            auto* current = sched->get_current();
            auto* next = sched->get_next();

            if (current) {
                float mult = current->expected_crowd / 100.0f;
                if (mult < 0.5f) mult = 0.5f;
                ss->interval = 1.f / (base_rate * mult);
            } else if (next && now > next->start_time_minutes - 15.f) {
                float mult = next->expected_crowd / 100.0f;
                if (mult < 0.5f) mult = 0.5f;
                ss->interval = 1.f / (base_rate * mult);
            } else {
                ss->interval = DEFAULT_SPAWN_INTERVAL;
            }
        }
    }
};

// Spawn agents at the spawn point on a timer
struct SpawnAgentSystem : System<> {
    void once(float dt) override {
        if (skip_game_logic()) return;
        auto* ss = EntityHelper::get_singleton_cmp<SpawnState>();
        if (!ss || !ss->enabled) return;

        auto* clock = EntityHelper::get_singleton_cmp<GameClock>();
        if (clock && clock->get_phase() == GameClock::Phase::DeadHours) return;

        ss->timer += dt;
        if (ss->timer >= ss->interval) {
            ss->timer -= ss->interval;
            auto [sx, sz] = best_stage_spot(SPAWN_X, SPAWN_Z);
            make_agent(SPAWN_X, SPAWN_Z, FacilityType::Stage, sx, sz);
        }
    }
};

// Difficulty scaling: increase spawn rate and crowd sizes over time
struct DifficultyScalingSystem : System<> {
    int last_hour = -1;

    void once(float) override {
        if (skip_game_logic()) return;
        auto* diff = EntityHelper::get_singleton_cmp<DifficultyState>();
        auto* clock = EntityHelper::get_singleton_cmp<GameClock>();
        auto* spawn = EntityHelper::get_singleton_cmp<SpawnState>();
        if (!diff || !clock || !spawn) return;

        int hour = clock->get_hour();
        if (last_hour >= 3 && last_hour < 10 && hour >= 10) {
            diff->day_number++;
            diff->spawn_rate_mult = 1.0f + (diff->day_number - 1) * 0.15f;
            diff->crowd_size_mult = 1.0f + (diff->day_number - 1) * 0.1f;
            spawn_toast(fmt::format("Day {} begins!", diff->day_number));
        }
        last_hour = hour;

        if (!spawn->manual_override) {
            spawn->interval = DEFAULT_SPAWN_INTERVAL / diff->spawn_rate_mult;
        }
    }
};

void register_schedule_update_systems(SystemManager& sm) {
    sm.register_update_system(std::make_unique<UpdateArtistScheduleSystem>());
}

void register_schedule_spawn_systems(SystemManager& sm) {
    sm.register_update_system(std::make_unique<SpawnAgentSystem>());
}

void register_schedule_difficulty_systems(SystemManager& sm) {
    sm.register_update_system(std::make_unique<DifficultyScalingSystem>());
}
