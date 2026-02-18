// Event domain: random events and per-frame effect flags.
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "afterhours/src/core/entity_query.h"
#include "components.h"
#include "engine/random_engine.h"
#include "systems.h"
#include "update_helpers.h"

// Track active event flags for other systems to query.
struct ApplyEventEffectsSystem : System<> {
    void once(float) override {
        event_flags::rain_active = false;
        event_flags::heat_active = false;

        if (skip_game_logic()) return;
        auto events = EntityQuery().whereHasComponent<ActiveEvent>().gen();

        for (Entity& ev_entity : events) {
            auto& ev = ev_entity.get<ActiveEvent>();
            switch (ev.type) {
                case EventType::Rain:
                    event_flags::rain_active = true;
                    break;
                case EventType::PowerOutage:
                    break;
                case EventType::VIPVisit:
                    break;
                case EventType::HeatWave:
                    event_flags::heat_active = true;
                    break;
            }
        }
    }
};

// Random events system: triggers events that affect gameplay
struct RandomEventSystem : System<> {
    void once(float dt) override {
        if (skip_game_logic()) return;
        auto* diff = EntityHelper::get_singleton_cmp<DifficultyState>();
        if (!diff) return;

        diff->event_timer += dt;

        auto active_events =
            EntityQuery().whereHasComponent<ActiveEvent>().gen();

        bool any_active = false;
        for (Entity& ev_entity : active_events) {
            auto& ev = ev_entity.get<ActiveEvent>();
            ev.elapsed += dt;
            if (ev.elapsed >= ev.duration) {
                spawn_toast(ev.description + " has ended.");
                ev_entity.cleanup = true;
            } else {
                any_active = true;
            }
        }

        if (diff->event_timer < diff->next_event_time) return;
        if (any_active) return;

        diff->event_timer = 0.f;
        auto& rng = RandomEngine::get();
        diff->next_event_time =
            rng.get_float(90.f, 180.f) / diff->spawn_rate_mult;

        int event_id = rng.get_int(0, 3);
        Entity& ev_entity = EntityHelper::createEntity();
        ev_entity.addComponent<ActiveEvent>();
        auto& ev = ev_entity.get<ActiveEvent>();

        switch (event_id) {
            case 0:
                ev.type = EventType::Rain;
                ev.duration = rng.get_float(30.f, 60.f);
                ev.description = "Rain storm";
                break;
            case 1:
                ev.type = EventType::PowerOutage;
                ev.duration = rng.get_float(15.f, 30.f);
                ev.description = "Power outage";
                break;
            case 2:
                ev.type = EventType::VIPVisit;
                ev.duration = rng.get_float(30.f, 60.f);
                ev.description = "VIP visit";
                break;
            case 3:
                ev.type = EventType::HeatWave;
                ev.duration = rng.get_float(20.f, 45.f);
                ev.description = "Heat wave";
                break;
        }

        spawn_toast("Event: " + ev.description + "!");
    }
};

void register_event_effect_systems(SystemManager& sm) {
    sm.register_update_system(std::make_unique<ApplyEventEffectsSystem>());
}

void register_event_random_systems(SystemManager& sm) {
    sm.register_update_system(std::make_unique<RandomEventSystem>());
}
