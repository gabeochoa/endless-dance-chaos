// Core update systems + orchestrator that registers all domain systems.
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "afterhours/src/core/entity_query.h"
#include "audio.h"
#include "components.h"
#include "entity_makers.h"
#include "save_system.h"
#include "systems.h"
#include "update_helpers.h"

// Domain registration functions (defined in separate .cpp files)
void register_event_effect_systems(SystemManager& sm);
void register_event_random_systems(SystemManager& sm);
void register_schedule_update_systems(SystemManager& sm);
void register_schedule_spawn_systems(SystemManager& sm);
void register_schedule_difficulty_systems(SystemManager& sm);
void register_building_systems(SystemManager& sm);
void register_agent_goal_systems(SystemManager& sm);
void register_agent_movement_systems(SystemManager& sm);
void register_crowd_flow_systems(SystemManager& sm);
void register_crowd_damage_systems(SystemManager& sm);
void register_crowd_particle_systems(SystemManager& sm);
void register_polish_systems(SystemManager& sm);

struct CameraInputSystem : System<ProvidesCamera> {
    void for_each_with(Entity&, ProvidesCamera& cam, float dt) override {
        cam.cam.handle_input(dt);
    }
};

// Advance game clock and detect phase changes
struct UpdateGameClockSystem : System<> {
    GameClock::Phase prev_phase = GameClock::Phase::Day;
    bool was_pause_down = false;

    void once(float dt) override {
        auto* clock = EntityHelper::get_singleton_cmp<GameClock>();
        if (!clock) return;

        if (!game_is_over()) {
            bool pause_down = action_down(InputAction::TogglePause);
            if (pause_down && !was_pause_down) {
                if (clock->speed == GameSpeed::Paused)
                    clock->speed = GameSpeed::OneX;
                else
                    clock->speed = GameSpeed::Paused;
                log_info("Game speed: {}",
                         clock->speed == GameSpeed::Paused ? "PAUSED" : "1x");
            }
            was_pause_down = pause_down;
        }

        float game_dt = (dt / GameClock::SECONDS_PER_GAME_MINUTE) *
                        clock->speed_multiplier();
        clock->game_time_minutes += game_dt;

        if (clock->game_time_minutes >= 1440.0f) {
            clock->game_time_minutes -= 1440.0f;
        }

        GameClock::Phase new_phase = clock->get_phase();
        if (new_phase != prev_phase) {
            log_info("Phase: {} -> {}", GameClock::phase_name(prev_phase),
                     GameClock::phase_name(new_phase));
            prev_phase = new_phase;
        }
    }
};

// Toggle data layer overlay with TAB, debug panel with backtick.
struct ToggleDataLayerSystem : System<> {
    bool was_data_layer_down = false;
    bool was_debug_down = false;

    void once(float) override {
        bool data_down = action_down(InputAction::ToggleDataLayer);
        if (data_down && !was_data_layer_down) {
            auto* gs = EntityHelper::get_singleton_cmp<GameState>();
            if (gs) {
                gs->show_data_layer = !gs->show_data_layer;
                log_info("Data layer: {}", gs->show_data_layer ? "ON" : "OFF");
            }
        }
        was_data_layer_down = data_down;

        bool debug_down = action_down(InputAction::ToggleUIDebug);
        if (debug_down && !was_debug_down) {
            auto* gs = EntityHelper::get_singleton_cmp<GameState>();
            if (gs) {
                gs->show_debug = !gs->show_debug;
                log_info("Debug panel: {}", gs->show_debug ? "ON" : "OFF");
                if (!gs->show_debug) {
                    auto* ss = EntityHelper::get_singleton_cmp<SpawnState>();
                    if (ss) ss->manual_override = false;
                    auto* clk = EntityHelper::get_singleton_cmp<GameClock>();
                    if (clk) clk->debug_time_mult = 0.f;
                }
            }
        }
        was_debug_down = debug_down;
    }
};

// Update toast messages and remove expired ones
struct UpdateToastsSystem : System<ToastMessage> {
    void for_each_with(Entity& e, ToastMessage& toast, float dt) override {
        toast.elapsed += dt;
        if (toast.elapsed >= toast.lifetime) {
            e.cleanup = true;
        }
    }
};

// Check if death count has reached max -> game over
struct CheckGameOverSystem : System<> {
    void once(float) override {
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        if (!gs || gs->is_game_over()) return;

        if (gs->death_count >= gs->max_deaths) {
            gs->status = GameStatus::GameOver;
            get_audio().play_gameover();
            get_audio().stop_music();
            save::update_meta_on_game_over();
            save::delete_save();
            log_info("GAME OVER: {} deaths reached", gs->death_count);
        }
    }
};

// SPACE restarts the game when in game over state
struct RestartGameSystem : System<> {
    void once(float) override {
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        if (!gs || !gs->is_game_over()) return;

        if (action_pressed(InputAction::Restart)) {
            log_info("Restarting game...");
            reset_game_state();
        }
    }
};

// Quick save/load system: F5 to save, F9 to load
struct SaveLoadSystem : System<> {
    void once(float) override {
        if (game_is_over()) return;
        if (action_pressed(InputAction::QuickSave)) {
            if (save::save_game()) {
                spawn_toast("Game saved!", 2.0f);
                log_info("Game saved to {}", save::SAVE_FILE);
            }
        }
        if (action_pressed(InputAction::QuickLoad)) {
            if (save::load_game()) {
                spawn_toast("Game loaded!", 2.0f);
                log_info("Game loaded from {}", save::SAVE_FILE);
            }
        }
    }
};

// Update audio: drive beat music based on stage performance state
struct UpdateAudioSystem : System<> {
    void once(float) override {
        if (!get_audio().initialized) return;
        auto* sched = EntityHelper::get_singleton_cmp<ArtistSchedule>();
        bool performing = sched && sched->stage_state == StageState::Performing;
        get_audio().update(performing && !game_is_over());
    }
};

void register_update_systems(SystemManager& sm) {
    // Core setup
    sm.register_update_system(std::make_unique<CameraInputSystem>());
    sm.register_update_system(std::make_unique<UpdateGameClockSystem>());

    // Events: apply effect flags before agent logic reads them
    register_event_effect_systems(sm);

    // Schedule: update artist state and spawn rates
    register_schedule_update_systems(sm);

    // Building: process placement before density calc
    register_building_systems(sm);

    // Core UI toggles
    sm.register_update_system(std::make_unique<ToggleDataLayerSystem>());

    // Agent goals: set targets before movement
    register_agent_goal_systems(sm);

    // Schedule: spawn new agents
    register_schedule_spawn_systems(sm);

    // Crowd flow: exodus, pheromone, density counting
    register_crowd_flow_systems(sm);

    // Agent movement: pathfinding and state transitions
    register_agent_movement_systems(sm);

    // Crowd damage: crush and death
    register_crowd_damage_systems(sm);

    // Core late
    sm.register_update_system(std::make_unique<UpdateToastsSystem>());
    sm.register_update_system(std::make_unique<CheckGameOverSystem>());
    sm.register_update_system(std::make_unique<RestartGameSystem>());

    // Crowd particles
    register_crowd_particle_systems(sm);

    // Events: spawn new random events
    register_event_random_systems(sm);

    // Schedule: difficulty scaling
    register_schedule_difficulty_systems(sm);

    // Polish: hints, bottleneck detection, death markers
    register_polish_systems(sm);

    // Core final
    sm.register_update_system(std::make_unique<SaveLoadSystem>());
    sm.register_update_system(std::make_unique<UpdateAudioSystem>());
}
