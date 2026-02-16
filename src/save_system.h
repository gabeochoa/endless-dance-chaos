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
static constexpr uint32_t SAVE_VERSION = 1;  // Increment on schema changes

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
    auto agents = afterhours::EntityQuery()
                      .whereHasComponent<Agent>()
                      .whereHasComponent<Transform>()
                      .gen();
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
    if (version != SAVE_VERSION) return false;  // Reject incompatible saves

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
