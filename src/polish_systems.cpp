// Polish domain: NUX hints, bottleneck detection, death marker decay.
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "afterhours/src/core/entity_query.h"
#include "components.h"
#include "systems.h"
#include "update_helpers.h"

static bool any_path_placed() {
    auto* grid = EntityHelper::get_singleton_cmp<Grid>();
    if (!grid) return false;
    for (int z = PLAY_MIN; z <= PLAY_MAX; z++)
        for (int x = PLAY_MIN; x <= PLAY_MAX; x++)
            if (grid->at(x, z).type == TileType::Path) return true;
    return false;
}

static bool any_agent_has_need() {
    auto agents = EntityQuery().whereHasComponent<AgentNeeds>().gen();
    for (Entity& e : agents) {
        auto& n = e.get<AgentNeeds>();
        if (n.needs_bathroom || n.needs_food) return true;
    }
    return false;
}

static bool any_tile_at_density_warning() {
    auto* grid = EntityHelper::get_singleton_cmp<Grid>();
    if (!grid) return false;
    int threshold = static_cast<int>(DENSITY_WARNING * MAX_AGENTS_PER_TILE);
    for (auto& tile : grid->tiles)
        if (tile.agent_count >= threshold) return true;
    return false;
}

static void create_nuxes() {
    int order = 0;

    auto make_nux = [&](const char* text, std::function<bool()> trigger,
                        std::function<bool()> complete) {
        Entity& e = EntityHelper::createEntity();
        e.addComponent<NuxHint>();
        auto& nux = e.get<NuxHint>();
        nux.text = text;
        nux.order = order++;
        nux.should_trigger = std::move(trigger);
        nux.is_complete = std::move(complete);
    };

    // 1. Build paths (dismissed when player places a path)
    make_nux(
        "Build paths from the GATE to the STAGE so attendees can find the "
        "music.",
        []() { return !any_path_placed(); },
        []() { return any_path_placed(); });

    // 2. First agents (dismissed when agents exist for 5+ seconds)
    make_nux(
        "Attendees are arriving! They follow paths to reach facilities.",
        []() {
            return EntityQuery().whereHasComponent<Agent>().gen_count() > 0;
        },
        []() { return false; });

    // 3. First need (dismissed when no agents currently have unmet needs,
    //    i.e. they found their way to a facility)
    make_nux(
        "An attendee needs a break! Make sure paths connect to facilities.",
        []() { return any_agent_has_need(); }, []() { return false; });

    // 4. First death (informational)
    make_nux(
        "An attendee was crushed! Spread crowds with more paths and "
        "facilities.",
        []() {
            auto* gs = EntityHelper::get_singleton_cmp<GameState>();
            return gs && gs->death_count > 0;
        },
        []() { return false; });

    // 5. Density warning (dismissed when player toggles overlay)
    make_nux(
        "Crowd density rising! Press TAB for the density overlay.",
        []() { return any_tile_at_density_warning(); },
        []() {
            auto* gs = EntityHelper::get_singleton_cmp<GameState>();
            return gs && gs->show_data_layer;
        });

    // 6. Night falls (informational)
    make_nux(
        "Night phase: bigger crowds are coming. Get ready!",
        []() {
            auto* clock = EntityHelper::get_singleton_cmp<GameClock>();
            return clock && clock->get_phase() == GameClock::Phase::Night;
        },
        []() { return false; });

    // 7. Exodus (informational)
    make_nux(
        "Exodus! Attendees are heading for the exits.",
        []() {
            auto* clock = EntityHelper::get_singleton_cmp<GameClock>();
            return clock && clock->get_phase() == GameClock::Phase::Exodus;
        },
        []() { return false; });

    // 8. Slot unlocked (dismissed when player places a new facility)
    make_nux(
        "New facility slot unlocked! Check your build bar.",
        []() {
            auto* gs = EntityHelper::get_singleton_cmp<GameState>();
            auto* fs = EntityHelper::get_singleton_cmp<FacilitySlots>();
            if (!gs || !fs) return false;
            return fs->get_slots_per_type(gs->max_attendees) > 1;
        },
        []() { return false; });

    EntityHelper::merge_entity_arrays();
}

// Manage the NUX queue: one active at a time, sequential
struct NuxSystem : System<> {
    void once(float dt) override {
        if (game_is_over()) return;
        auto* nm = EntityHelper::get_singleton_cmp<NuxManager>();
        if (!nm) return;

        if (!nm->initialized) {
            create_nuxes();
            nm->initialized = true;
        }

        // Find the currently active NUX
        Entity* active = nullptr;
        {
            auto nuxes = EntityQuery().whereHasComponent<NuxHint>().gen();
            for (Entity& e : nuxes) {
                auto& nux = e.get<NuxHint>();
                if (nux.is_active) {
                    active = &e;
                    break;
                }
            }
        }

        // Process the active NUX
        if (active) {
            auto& nux = active->get<NuxHint>();
            nux.time_shown += dt;

            if (nux.was_dismissed || (nux.is_complete && nux.is_complete())) {
                nux.is_active = false;
                nux.was_dismissed = true;
                active = nullptr;
            }
        }

        // If nothing active, find the next eligible NUX (lowest order first)
        if (!active) {
            auto nuxes = EntityQuery().whereHasComponent<NuxHint>().gen();
            Entity* best = nullptr;
            int best_order = 99999;
            for (Entity& e : nuxes) {
                auto& nux = e.get<NuxHint>();
                if (nux.is_active || nux.was_dismissed) continue;
                if (nux.order < best_order && nux.should_trigger &&
                    nux.should_trigger()) {
                    best = &e;
                    best_order = nux.order;
                }
            }
            if (best) {
                best->get<NuxHint>().is_active = true;
                best->get<NuxHint>().time_shown = 0.f;
            }
        }
    }
};

// Detect overwhelmed facilities and warn the player
struct BottleneckCheckSystem : System<> {
    void once(float dt) override {
        if (skip_game_logic()) return;
        auto* nm = EntityHelper::get_singleton_cmp<NuxManager>();
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        auto* fs = EntityHelper::get_singleton_cmp<FacilitySlots>();
        if (!nm || !grid || !gs || !fs) return;

        grid->ensure_caches();

        auto check_facility =
            [&](const std::vector<std::pair<int, int>>& positions, float& timer,
                bool& warned, FacilityType ftype, const char* name) {
                if (warned) return;
                if (!fs->can_place(ftype, gs->max_attendees)) return;

                int total_agents = 0;
                for (auto [x, z] : positions)
                    total_agents += grid->at(x, z).agent_count;

                int capacity = FACILITY_MAX_AGENTS;
                float ratio =
                    total_agents / static_cast<float>(std::max(capacity, 1));

                if (ratio >= 0.9f)
                    timer += dt;
                else
                    timer = 0.f;

                if (timer >= 5.f) {
                    warned = true;
                    spawn_toast(
                        fmt::format("{} is overwhelmed — build another!", name),
                        5.0f);
                }
            };

        check_facility(grid->bathroom_positions, nm->bathroom_overload_timer,
                       nm->bathroom_warned, FacilityType::Bathroom, "Bathroom");
        check_facility(grid->food_positions, nm->food_overload_timer,
                       nm->food_warned, FacilityType::Food, "Food stall");
        check_facility(grid->medtent_positions, nm->medtent_overload_timer,
                       nm->medtent_warned, FacilityType::MedTent, "Med tent");
    }
};

// Decay death markers and remove expired ones
struct UpdateDeathMarkersSystem : System<DeathMarker> {
    void for_each_with(Entity& e, DeathMarker& dm, float dt) override {
        if (game_is_paused()) return;
        dm.lifetime -= dt;
        if (dm.lifetime <= 0.f) e.cleanup = true;
    }
};

void register_polish_systems(SystemManager& sm) {
    sm.register_update_system(std::make_unique<NuxSystem>());
    sm.register_update_system(std::make_unique<BottleneckCheckSystem>());
    sm.register_update_system(std::make_unique<UpdateDeathMarkersSystem>());
}
