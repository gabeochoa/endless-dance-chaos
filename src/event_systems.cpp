// Event domain: the event director (seeded, 0-2 events per game-day),
// per-frame effect flags, and viral spread.
//
// Director picks come from the EVENT_DEFS data table in components.h.
// The director owns a dedicated RNG stream (seeded via set_event_seed),
// separate from the shared RandomEngine stream that movement,
// pathfinding, and spawning draw from, so a fixed event seed yields a
// fixed event sequence regardless of what agents do.
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include <functional>
#include <random>

#include "afterhours/src/core/entity_helper.h"
#include "afterhours/src/core/entity_query.h"
#include "components.h"
#include "engine/random_engine.h"
#include "entity_makers.h"
#include "systems.h"
#include "update_helpers.h"

// ── Dedicated director RNG ─────────────────────────────────────────────
// Same seeding pattern as RandomEngine: hash the seed string, seed a
// mt19937 with it. Deliberately NOT the shared RandomEngine: agent
// movement consumes that stream every frame, so director picks would
// otherwise depend on crowd behaviour.
static std::mt19937& event_rng() {
    static std::mt19937 rng{std::hash<std::string>{}("default_seed")};
    return rng;
}

static int event_rng_int(int a, int b) {
    if (a > b) std::swap(a, b);
    std::uniform_int_distribution<int> dist(a, b);
    return dist(event_rng());
}

static float event_rng_float(float a, float b) {
    if (a > b) std::swap(a, b);
    std::uniform_real_distribution<float> dist(a, b);
    return dist(event_rng());
}

static const char* facility_type_name(FacilityType type) {
    switch (type) {
        case FacilityType::Bathroom:
            return "Bathroom";
        case FacilityType::Food:
            return "Food";
        case FacilityType::Stage:
            return "Stage";
        case FacilityType::Exit:
            return "Exit";
        case FacilityType::MedTent:
            return "MedTent";
        case FacilityType::Bar:
            return "Bar";
        case FacilityType::WaterStation:
            return "WaterStation";
    }
    return "Unknown";
}

// Facility types an Equipment Failure can take offline.
static constexpr FacilityType OFFLINE_CANDIDATES[] = {
    FacilityType::Bathroom, FacilityType::Food, FacilityType::MedTent,
    FacilityType::Bar, FacilityType::WaterStation};

// Pick the offline facility: prefer a type actually present on the map
// (an outage at a facility that does not exist has no visible effect).
// `explicit_type` >= 0 pins the choice (E2E trigger_event).
static FacilityType pick_offline_type(int explicit_type) {
    if (explicit_type >= 0) {
        // Explicit pins only count for real service facilities; Stage /
        // Exit / out-of-range values fall through to a random pick.
        for (FacilityType cand : OFFLINE_CANDIDATES) {
            if (explicit_type == static_cast<int>(cand)) return cand;
        }
    }
    auto* grid = EntityHelper::get_singleton_cmp<Grid>();
    std::vector<FacilityType> present;
    if (grid) {
        grid->ensure_caches();
        for (FacilityType cand : OFFLINE_CANDIDATES) {
            if (!grid->get_facility_positions(facility_type_to_tile(cand))
                     .empty())
                present.push_back(cand);
        }
    }
    if (present.empty()) return FacilityType::Bathroom;
    // Director-forced picks use the event RNG; forced picks (trigger_event
    // without an explicit type) use the shared stream so they never
    // disturb the director's deterministic sequence.
    if (explicit_type == -2) {
        return present[static_cast<size_t>(
            event_rng_int(0, static_cast<int>(present.size()) - 1))];
    }
    return present[static_cast<size_t>(
        RandomEngine::get().get_int(0, static_cast<int>(present.size()) - 1))];
}

// Announce one fired event on both channels: a toast (immediate) and
// the NEWS ticker queue (drained once per render frame).
static void announce_event(const std::string& text) {
    spawn_toast(text);
    auto* director = EntityHelper::get_singleton_cmp<EventDirectorState>();
    if (director) {
        if (director->announcement_queue.size() > 16)
            director->announcement_queue.erase(
                director->announcement_queue.begin());
        director->announcement_queue.push_back(text);
    }
}

// Fire one event now. Timed events create an ActiveEvent entity;
// instant events apply immediately and create no entity.
void fire_event(EventType type, float duration, int offline_type) {
    const EventDef* def = event_def_for(type);
    std::string name = def ? def->name : "Unknown event";

    if (type == EventType::SurpriseInspection) {
        run_inspection_now();
        announce_event("Event: Surprise Inspection!");
        log_info("[Event] Surprise Inspection fired (inspection ran now)");
        return;
    }
    if (type == EventType::GenerousSponsor) {
        int tier = 0;
        if (auto* lineup = EntityHelper::get_singleton_cmp<LineupState>())
            tier = lineup->tier;
        int grant = sponsor_grant_for_tier(tier);
        if (auto* econ = EntityHelper::get_singleton_cmp<Economy>())
            econ->earn(grant);
        announce_event(fmt::format("Event: Generous Sponsor! +${} (Tier {}).",
                                   grant, tier));
        log_info("[Event] Generous Sponsor fired: grant ${} at tier {}", grant,
                 tier);
        return;
    }

    Entity& ev_entity = EntityHelper::createEntity();
    ev_entity.addComponent<ActiveEvent>();
    auto& ev = ev_entity.get<ActiveEvent>();
    ev.type = type;
    ev.duration = duration;
    ev.description = name;
    std::string text = "Event: " + name + "!";
    if (type == EventType::EquipmentFailure) {
        // -1 = pick via the shared stream (forced trigger);
        // -2 = pick via the director's RNG stream (director picks).
        ev.offline_type = pick_offline_type(offline_type);
        text = fmt::format("Event: Equipment Failure! {} offline.",
                           facility_type_name(ev.offline_type));
        ev.description = fmt::format("Equipment Failure ({})",
                                     facility_type_name(ev.offline_type));
    }
    EntityHelper::merge_entity_arrays();
    announce_event(text);
    log_info("[Event] {} fired for {:.1f}s", name, duration);
}

static void reset_director_singleton() {
    auto* director = EntityHelper::get_singleton_cmp<EventDirectorState>();
    if (!director) return;
    director->initialized = false;
    director->last_day = 1;
    director->events_fired_today = 0;
    director->events_target_today = 0;
    director->next_fire_in = 0.f;
    director->fired_today.clear();
    director->fired_yesterday.clear();
    director->announcement_queue.clear();
    director->vip_patience_seconds = VIP_PATIENCE_SECONDS;
}

void reset_event_director() {
    event_rng().seed(std::hash<std::string>{}("default_seed"));
    reset_director_singleton();
    auto* director = EntityHelper::get_singleton_cmp<EventDirectorState>();
    if (director) director->seed = "default_seed";
}

void set_event_seed(const std::string& seed) {
    event_rng().seed(std::hash<std::string>{}(seed));
    auto* director = EntityHelper::get_singleton_cmp<EventDirectorState>();
    if (director) director->seed = seed;
    // Fresh bookkeeping from this point: the next frame rolls a new day
    // target from the fresh stream for the current day.
    reset_director_singleton();
    if (director) director->seed = seed;
}

// Track active event flags for other systems to query.
struct ApplyEventEffectsSystem : System<> {
    void once(float) override {
        event_flags::rain_active = false;
        event_flags::heat_active = false;
        event_flags::viral_active = false;
        event_flags::facility_offline_flags.fill(false);

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
                case EventType::ViralOutbreak:
                    event_flags::viral_active = true;
                    break;
                case EventType::SurpriseInspection:
                case EventType::GenerousSponsor:
                    break;  // instant: no lingering flag
                case EventType::EquipmentFailure:
                    event_flags::facility_offline_flags[static_cast<size_t>(
                        ev.offline_type)] = true;
                    break;
            }
        }
    }
};

// Viral Outbreak spread: a healthy agent standing near an ill agent
// (needs_heat) accumulates exposure; at EVENT_VIRAL_EXPOSURE_SECONDS it
// catches the illness. Exposure decays when no ill agent is nearby.
struct ViralSpreadSystem : System<> {
    void once(float dt) override {
        if (skip_game_logic()) return;
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;

        auto agents = EntityQuery()
                          .whereHasComponent<Agent, AgentNeeds, Transform>()
                          .gen();
        if (!event_flags::viral_active) {
            for (Entity& e : agents) {
                auto& needs = e.get<AgentNeeds>();
                if (needs.viral_exposure > 0.f) needs.viral_exposure = 0.f;
            }
            return;
        }

        // Pass 1: which tiles hold an ill agent (not one being serviced:
        // isolated in a facility, not spreading through the crowd).
        std::array<bool, MAP_SIZE* MAP_SIZE> ill = {};
        for (Entity& e : agents) {
            if (!e.is_missing<BeingServiced>()) continue;
            auto& needs = e.get<AgentNeeds>();
            if (!needs.needs_heat) continue;
            auto& tf = e.get<Transform>();
            auto [gx, gz] = grid->world_to_grid(tf.position.x, tf.position.y);
            if (grid->in_bounds(gx, gz))
                ill[static_cast<size_t>(grid->index(gx, gz))] = true;
        }

        // Pass 2: healthy agents near an ill agent accumulate exposure.
        int radius = static_cast<int>(EVENT_VIRAL_SPREAD_RADIUS);
        for (Entity& e : agents) {
            if (!e.is_missing<BeingServiced>()) continue;
            if (!e.is_missing<Collapsed>()) continue;
            auto& needs = e.get<AgentNeeds>();
            if (needs.needs_heat) continue;
            auto& tf = e.get<Transform>();
            auto [gx, gz] = grid->world_to_grid(tf.position.x, tf.position.y);
            if (!grid->in_bounds(gx, gz)) continue;
            bool exposed = false;
            for (int dz = -radius; dz <= radius && !exposed; dz++) {
                for (int dx = -radius; dx <= radius; dx++) {
                    int nx = gx + dx, nz = gz + dz;
                    if (!grid->in_bounds(nx, nz)) continue;
                    if (ill[static_cast<size_t>(grid->index(nx, nz))]) {
                        exposed = true;
                        break;
                    }
                }
            }
            if (exposed) {
                needs.viral_exposure += dt;
                if (needs.viral_exposure >= EVENT_VIRAL_EXPOSURE_SECONDS) {
                    needs.needs_heat = true;
                    needs.viral_exposure = 0.f;
                }
            } else if (needs.viral_exposure > 0.f) {
                needs.viral_exposure = std::max(0.f, needs.viral_exposure - dt);
            }
        }
    }
};

// ── Director ─────────────────────────────────────────────────────────────

// Roll a new day: yesterday's picks become the no-repeat exclusion set,
// today's quota is 0-2, and the first event fires 90-180s in — the old
// RandomEventSystem's cadence, so long deterministic scenes (e.g. a 51s
// stage-entry walk) never get a surprise mid-scene event.
static void director_new_day(EventDirectorState& director, int day_number) {
    director.fired_yesterday = director.fired_today;
    director.fired_today.clear();
    director.events_fired_today = 0;
    director.last_day = day_number;
    director.events_target_today = event_rng_int(0, 2);
    director.next_fire_in = event_rng_float(90.f, 180.f);
    director.initialized = true;
    log_info("[Event] Day {}: director target = {} events", day_number,
             director.events_target_today);
}

// Pick one event type for today: weighted by the table, not fired
// yesterday, not already fired today. Returns false when nothing is
// eligible (only possible if the exclusion sets covered the table).
static bool director_pick_type(const EventDirectorState& director,
                               EventType& out) {
    std::vector<const EventDef*> eligible;
    for (const auto& def : EVENT_DEFS) {
        if (def.weight <= 0) continue;
        // VIP visits are high-tier only: below VIP_MIN_TIER the park
        // is not famous enough to draw one, so the event stays out
        // of the eligible set (and out of the weight total — low-tier
        // director sequences are unchanged).
        if (def.type == EventType::VIPVisit) {
            auto* lineup = EntityHelper::get_singleton_cmp<LineupState>();
            if (!lineup || lineup->tier < VIP_MIN_TIER) continue;
        }
        int v = static_cast<int>(def.type);
        if (std::find(director.fired_yesterday.begin(),
                      director.fired_yesterday.end(),
                      v) != director.fired_yesterday.end())
            continue;
        if (std::find(director.fired_today.begin(), director.fired_today.end(),
                      v) != director.fired_today.end())
            continue;
        eligible.push_back(&def);
    }
    if (eligible.empty()) return false;
    int total_weight = 0;
    for (const auto* d : eligible) total_weight += d->weight;
    int roll = event_rng_int(0, total_weight - 1);
    for (const auto* d : eligible) {
        roll -= d->weight;
        if (roll < 0) {
            out = d->type;
            return true;
        }
    }
    out = eligible.back()->type;
    return true;
}

// Fire one director-picked event for today and record it.
static bool director_fire_one(EventDirectorState& director) {
    EventType type;
    if (!director_pick_type(director, type)) {
        log_warn("[Event] Director: no eligible event type for today");
        return false;
    }
    const EventDef* def = event_def_for(type);
    float duration =
        def ? event_rng_float(def->duration_min, def->duration_max) : 0.f;
    // EquipmentFailure: -2 routes the offline pick through the
    // director's own RNG stream (deterministic under set_event_seed);
    // -1 would use the shared RandomEngine stream instead.
    int offline = type == EventType::EquipmentFailure ? -2 : -1;
    fire_event(type, duration, offline);
    director.fired_today.push_back(static_cast<int>(type));
    director.events_fired_today++;
    return true;
}

int director_roll_today() {
    auto* diff = EntityHelper::get_singleton_cmp<DifficultyState>();
    auto* director = EntityHelper::get_singleton_cmp<EventDirectorState>();
    if (!diff || !director) return 0;
    if (!director->initialized || diff->day_number != director->last_day)
        director_new_day(*director, diff->day_number);
    int fired = 0;
    while (director->events_fired_today < director->events_target_today) {
        if (!director_fire_one(*director)) break;
        fired++;
    }
    return fired;
}

// Event director system: expires timed events, rolls each new day's
// quota, and fires the day's events on a timer.
struct EventDirectorSystem : System<> {
    void once(float dt) override {
        if (skip_game_logic()) return;
        auto* diff = EntityHelper::get_singleton_cmp<DifficultyState>();
        auto* director = EntityHelper::get_singleton_cmp<EventDirectorState>();
        if (!diff || !director) return;

        auto active_events =
            EntityQuery().whereHasComponent<ActiveEvent>().gen();
        for (Entity& ev_entity : active_events) {
            auto& ev = ev_entity.get<ActiveEvent>();
            ev.elapsed += dt;
            if (ev.elapsed >= ev.duration) {
                spawn_toast(ev.description + " has ended.");
                ev_entity.cleanup = true;
            }
        }

        if (!director->initialized || diff->day_number != director->last_day)
            director_new_day(*director, diff->day_number);

        if (director->events_fired_today >= director->events_target_today)
            return;
        director->next_fire_in -= dt;
        if (director->next_fire_in > 0.f) return;
        if (director_fire_one(*director)) {
            director->next_fire_in = event_rng_float(90.f, 180.f);
        } else {
            director->next_fire_in = event_rng_float(90.f, 180.f);
        }
    }
};

// VIP arrival: while a VIPVisit event is active, one VIP guest walks
// in after VIP_ARRIVAL_DELAY_SECONDS — but only at/above VIP_MIN_TIER,
// and only if no VIP is already in the park (a second firing while
// one is present does not stack guests). A VIPVisit fired below the
// tier still announces and still counts as a fired event; it just
// spawns no guest. The delay is deliberate: the toast announces the
// visit first, and arrival itself is a timed condition.
struct VIPArrivalSystem : System<> {
    float timer = 0.f;

    void once(float dt) override {
        if (skip_game_logic()) return;
        // One guest per event: ActiveEvent::notified is this event's
        // already-spawned latch (it had no other reader). A VIP who
        // leaves unhappy mid-event is NOT replaced.
        Entity* vip_event = nullptr;
        auto events = EntityQuery().whereHasComponent<ActiveEvent>().gen();
        for (Entity& ev : events) {
            auto& act = ev.get<ActiveEvent>();
            if (act.type == EventType::VIPVisit && !act.notified) {
                vip_event = &ev;
                break;
            }
        }
        if (!vip_event) {
            timer = 0.f;
            return;
        }
        timer += dt;
        if (timer < VIP_ARRIVAL_DELAY_SECONDS) return;
        if (EntityQuery().whereHasComponent<VIPGuest>().gen_count() > 0) return;
        auto* lineup = EntityHelper::get_singleton_cmp<LineupState>();
        if (!lineup || lineup->tier < VIP_MIN_TIER) return;
        vip_event->get<ActiveEvent>().notified = true;
        timer = 0.f;
        // Arrive through the gate like any other guest (pays the
        // ticket via make_agent), then walk to their need.
        auto [sx, sz] = best_stage_spot(SPAWN_X, SPAWN_Z);
        Entity& vip = make_agent(SPAWN_X, SPAWN_Z, FacilityType::Stage, sx, sz);
        vip.addComponent<VIPGuest>();
        auto& guest = vip.get<VIPGuest>();
        if (auto* director =
                EntityHelper::get_singleton_cmp<EventDirectorState>())
            guest.patience = director->vip_patience_seconds;
        if (!vip.is_missing<AgentNeeds>()) {
            auto& needs = vip.get<AgentNeeds>();
            needs.needs_food = true;
            needs.food_timer = needs.food_threshold;
        }
        EntityHelper::merge_entity_arrays();
        spawn_toast(
            "A VIP guest has arrived! Serve them quickly for a "
            "bonus.",
            5.0f);
        log_info("[Event] VIP guest arrived (tier {})", lineup->tier);
    }
};

void register_event_effect_systems(SystemManager& sm) {
    sm.register_update_system(std::make_unique<ApplyEventEffectsSystem>());
    sm.register_update_system(std::make_unique<ViralSpreadSystem>());
    sm.register_update_system(std::make_unique<VIPArrivalSystem>());
}

void register_event_random_systems(SystemManager& sm) {
    sm.register_update_system(std::make_unique<EventDirectorSystem>());
}
