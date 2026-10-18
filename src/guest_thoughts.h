#pragma once

// Guest thoughts (RCT2 guests-window style): the 5 canonical thoughts,
// counted from live agent states each time they are needed. Shared by
// the Guests tab (render_ui.cpp), the NEWS ticker quote
// (ticker_poll_events), and the E2E thought assertions, so all three
// always agree on the ranking.
//
// Mapping: toilet <- bathroom need; value <- no unmet needs (content
// guests); crowded <- standing on a dangerously dense tile; sick <-
// heat/illness need; lineup <- watching while an act is performing or
// announcing. Sorted by count descending; ties keep canonical order
// (stable sort), so a forced bathroom need makes the toilet thought
// the top thought.

#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "afterhours/src/core/entity_query.h"
#include "components.h"
#include "game.h"

using namespace afterhours;

struct GuestThought {
    const char* text;
    int count = 0;
};

inline std::vector<GuestThought> compute_guest_thoughts() {
    std::vector<GuestThought> thoughts = {
        {"I need a toilet", 0}, {"Great value!", 0},    {"Too crowded", 0},
        {"I feel sick", 0},     {"Love the lineup", 0},
    };

    auto* grid = EntityHelper::get_singleton_cmp<Grid>();
    auto* sched = EntityHelper::get_singleton_cmp<ArtistSchedule>();
    bool act_playing = sched && (sched->stage_state == StageState::Performing ||
                                 sched->stage_state == StageState::Announcing);
    int danger_threshold =
        static_cast<int>(DENSITY_DANGEROUS * MAX_AGENTS_PER_TILE);

    auto agents = EntityQuery().whereHasComponent<Agent, AgentNeeds>().gen();
    for (Entity& e : agents) {
        auto& needs = e.get<AgentNeeds>();
        if (needs.needs_bathroom) thoughts[0].count++;
        if (needs.needs_heat) thoughts[3].count++;
        if (!needs.needs_bathroom && !needs.needs_food && !needs.needs_heat &&
            !needs.needs_bar)
            thoughts[1].count++;
        if (act_playing && !e.is_missing<WatchingStage>()) thoughts[4].count++;
        if (grid && !e.is_missing<Transform>()) {
            auto& tf = e.get<Transform>();
            auto [gx, gz] = grid->world_to_grid(tf.position.x, tf.position.y);
            if (grid->in_bounds(gx, gz) &&
                grid->at(gx, gz).agent_count >= danger_threshold)
                thoughts[2].count++;
        }
    }

    std::stable_sort(thoughts.begin(), thoughts.end(),
                     [](const GuestThought& a, const GuestThought& b) {
                         return a.count > b.count;
                     });
    return thoughts;
}

// Look up one canonical thought by E2E key ("toilet", "value",
// "crowded", "sick", "lineup"). Returns nullptr for an unknown key.
inline const char* guest_thought_text_for_key(const std::string& key) {
    if (key == "toilet") return "I need a toilet";
    if (key == "value") return "Great value!";
    if (key == "crowded") return "Too crowded";
    if (key == "sick") return "I feel sick";
    if (key == "lineup") return "Love the lineup";
    return nullptr;
}
