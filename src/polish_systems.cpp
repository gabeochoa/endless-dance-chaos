// Polish domain: contextual hints, bottleneck detection, death marker decay.
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "afterhours/src/core/entity_query.h"
#include "components.h"
#include "systems.h"
#include "update_helpers.h"

// Fire contextual hints based on game state transitions
struct HintCheckSystem : System<> {
    void once(float dt) override {
        if (game_is_over()) return;
        auto* hs = EntityHelper::get_singleton_cmp<HintState>();
        if (!hs) return;

        hs->game_elapsed += dt;

        auto try_hint = [&](HintState::Hint id, const char* msg) {
            if (hs->shown.test(id)) return;
            hs->shown.set(id);
            spawn_toast(msg, 6.0f, true);
        };

        // Game start: after 5 seconds, if no paths placed
        if (!hs->shown.test(HintState::GameStart) && hs->game_elapsed >= 5.f) {
            auto* grid = EntityHelper::get_singleton_cmp<Grid>();
            if (grid) {
                bool has_path = false;
                for (int z = PLAY_MIN; z <= PLAY_MAX && !has_path; z++)
                    for (int x = PLAY_MIN; x <= PLAY_MAX && !has_path; x++)
                        if (grid->at(x, z).type == TileType::Path)
                            has_path = true;
                if (!has_path)
                    try_hint(HintState::GameStart,
                             "Build paths from the GATE to the STAGE so "
                             "attendees can find the music.");
            }
        }

        // First agents
        if (!hs->shown.test(HintState::FirstAgents)) {
            if (EntityQuery().whereHasComponent<Agent>().gen_count() > 0)
                try_hint(HintState::FirstAgents,
                         "Attendees are arriving! They follow paths to "
                         "reach facilities.");
        }

        // First need
        if (!hs->shown.test(HintState::FirstNeed)) {
            auto agents = EntityQuery().whereHasComponent<AgentNeeds>().gen();
            for (Entity& e : agents) {
                auto& n = e.get<AgentNeeds>();
                if (n.needs_bathroom || n.needs_food) {
                    try_hint(HintState::FirstNeed,
                             "An attendee needs a break! Make sure paths "
                             "connect to facilities.");
                    break;
                }
            }
        }

        // First death
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        if (gs && !hs->shown.test(HintState::FirstDeath)) {
            if (gs->death_count > 0 && hs->prev_death_count == 0)
                try_hint(HintState::FirstDeath,
                         "An attendee was crushed! Spread crowds with "
                         "more paths and facilities.");
        }
        if (gs) hs->prev_death_count = gs->death_count;

        // First density warning
        if (!hs->shown.test(HintState::FirstDensity)) {
            auto* grid = EntityHelper::get_singleton_cmp<Grid>();
            if (grid) {
                int threshold =
                    static_cast<int>(DENSITY_WARNING * MAX_AGENTS_PER_TILE);
                for (auto& tile : grid->tiles) {
                    if (tile.agent_count >= threshold) {
                        try_hint(HintState::FirstDensity,
                                 "Crowd density rising! Press TAB for "
                                 "the density overlay.");
                        break;
                    }
                }
            }
        }

        // Night falls / Exodus
        auto* clock = EntityHelper::get_singleton_cmp<GameClock>();
        if (clock) {
            auto phase = clock->get_phase();
            if (phase != hs->prev_phase) {
                if (phase == GameClock::Phase::Night)
                    try_hint(HintState::NightFalls,
                             "Night phase: bigger crowds are coming. "
                             "Get ready!");
                if (phase == GameClock::Phase::Exodus)
                    try_hint(HintState::FirstExodus,
                             "Exodus! Attendees are heading for the exits.");
            }
            hs->prev_phase = phase;
        }

        // Slot unlocked
        auto* fs = EntityHelper::get_singleton_cmp<FacilitySlots>();
        if (fs && gs) {
            int cur = fs->get_slots_per_type(gs->max_attendees);
            if (hs->prev_slots_per_type > 0 && cur > hs->prev_slots_per_type)
                try_hint(HintState::SlotUnlocked,
                         "New facility slot unlocked! Check your build bar.");
            hs->prev_slots_per_type = cur;
        }
    }
};

// Detect overwhelmed facilities and warn the player
struct BottleneckCheckSystem : System<> {
    float check_timer = 0.f;

    void once(float dt) override {
        if (skip_game_logic()) return;
        auto* hs = EntityHelper::get_singleton_cmp<HintState>();
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        auto* fs = EntityHelper::get_singleton_cmp<FacilitySlots>();
        if (!hs || !grid || !gs || !fs) return;

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

        check_facility(grid->bathroom_positions, hs->bathroom_overload_timer,
                       hs->bathroom_warned, FacilityType::Bathroom, "Bathroom");
        check_facility(grid->food_positions, hs->food_overload_timer,
                       hs->food_warned, FacilityType::Food, "Food stall");
        check_facility(grid->medtent_positions, hs->medtent_overload_timer,
                       hs->medtent_warned, FacilityType::MedTent, "Med tent");
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
    sm.register_update_system(std::make_unique<HintCheckSystem>());
    sm.register_update_system(std::make_unique<BottleneckCheckSystem>());
    sm.register_update_system(std::make_unique<UpdateDeathMarkersSystem>());
}
