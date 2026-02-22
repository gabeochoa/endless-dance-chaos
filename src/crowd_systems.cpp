// Crowd domain: density, pheromone, exodus, crush damage, death, particles,
// stats.
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "afterhours/src/core/entity_query.h"
#include "audio.h"
#include "components.h"
#include "engine/random_engine.h"
#include "systems.h"
#include "update_helpers.h"

// During Exodus, flood exit pheromone from gates using BFS
static void flood_exit_pheromone(Grid& grid) {
    std::queue<std::pair<int, int>> frontier;
    for (int z = 0; z < MAP_SIZE; z++) {
        for (int x = 0; x < MAP_SIZE; x++) {
            if (grid.at(x, z).type == TileType::Gate) {
                grid.at(x, z).pheromone[Tile::PHERO_EXIT] = 255;
                frontier.push({x, z});
            }
        }
    }
    constexpr int dirs[][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    while (!frontier.empty()) {
        auto [x, z] = frontier.front();
        frontier.pop();
        uint8_t current = grid.at(x, z).pheromone[Tile::PHERO_EXIT];
        if (current <= 5) continue;
        for (auto [dx, dz] : dirs) {
            int nx = x + dx, nz = z + dz;
            if (!grid.in_bounds(nx, nz)) continue;
            TileType type = grid.at(nx, nz).type;
            if (type == TileType::Fence || type == TileType::Stage) continue;
            if (grid.at(nx, nz).pheromone[Tile::PHERO_EXIT] < current - 5) {
                grid.at(nx, nz).pheromone[Tile::PHERO_EXIT] = current - 5;
                frontier.push({nx, nz});
            }
        }
    }
}

// During Exodus phase: switch agents to exit mode and emit exit pheromone
struct ExodusSystem : System<> {
    bool flooded_this_exodus = false;
    GameClock::Phase prev_phase = GameClock::Phase::Day;

    void once(float) override {
        if (skip_game_logic()) return;
        auto* clock = EntityHelper::get_singleton_cmp<GameClock>();
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!clock || !grid) return;

        auto phase = clock->get_phase();

        if (phase == GameClock::Phase::Exodus &&
            prev_phase != GameClock::Phase::Exodus) {
            flooded_this_exodus = false;
            log_info("Exodus begins — gates emitting exit pheromone");
        }

        if (phase == GameClock::Phase::DeadHours &&
            prev_phase == GameClock::Phase::Exodus) {
            auto* gs = EntityHelper::get_singleton_cmp<GameState>();
            int count = 0;
            auto agents = EntityQuery().whereHasComponent<Agent>().gen();
            for (Entity& e : agents) {
                if (e.is_missing<CarryoverAgent>()) {
                    e.addComponent<CarryoverAgent>();
                    count++;
                }
            }
            if (gs) gs->carryover_count = count;
            if (count > 0) log_info("Carryover: {} agents stuck", count);
        }

        prev_phase = phase;

        if (phase != GameClock::Phase::Exodus) return;

        if (!flooded_this_exodus) {
            flood_exit_pheromone(*grid);
            flooded_this_exodus = true;
        }

        grid->ensure_caches();
        for (auto [gx, gz] : grid->gate_positions) {
            grid->at(gx, gz).pheromone[Tile::PHERO_EXIT] = 255;
        }

        auto agents = EntityQuery().whereHasComponent<Agent>().gen();
        for (Entity& e : agents) {
            auto& agent = e.get<Agent>();
            if (agent.want != FacilityType::Exit) {
                agent.want = FacilityType::Exit;
                agent.target_grid_x = GATE_X;
                agent.target_grid_z = GATE_Z1;
            }
        }
    }
};

// Remove agents that reach a gate during Exodus
struct GateExitSystem : System<Agent, Transform> {
    void for_each_with(Entity& e, Agent& agent, Transform& tf, float) override {
        if (skip_game_logic()) return;
        auto* clock = EntityHelper::get_singleton_cmp<GameClock>();
        if (!clock || clock->get_phase() != GameClock::Phase::Exodus) return;
        if (agent.want != FacilityType::Exit) return;

        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;

        auto [gx, gz] = grid->world_to_grid(tf.position.x, tf.position.y);
        if (!grid->in_bounds(gx, gz)) return;

        if (grid->at(gx, gz).type == TileType::Gate) {
            auto* gs = EntityHelper::get_singleton_cmp<GameState>();
            if (gs) gs->agents_exited++;
            e.cleanup = true;
        }
    }
};

// Deposit pheromones as agents walk away from facilities
struct PheromoneDepositSystem : System<Agent, Transform, PheromoneDepositor> {
    void for_each_with(Entity&, Agent&, Transform& tf, PheromoneDepositor& dep,
                       float) override {
        if (skip_game_logic()) return;
        if (!dep.is_depositing) return;
        if (dep.deposit_distance >= PheromoneDepositor::MAX_DEPOSIT_DISTANCE) {
            dep.is_depositing = false;
            return;
        }

        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;

        auto [gx, gz] = grid->world_to_grid(tf.position.x, tf.position.y);
        if (!grid->in_bounds(gx, gz)) return;

        int ch = facility_to_channel(dep.leaving_type);
        auto& val = grid->at(gx, gz).pheromone[ch];
        int nv = static_cast<int>(val) + 50;
        val = static_cast<uint8_t>(std::min(nv, 255));
        dep.deposit_distance += 1.0f;
    }
};

// Decay all pheromones periodically
struct DecayPheromonesSystem : System<> {
    float accumulator = 0.f;
    static constexpr float DECAY_INTERVAL = 1.5f;

    void once(float dt) override {
        if (skip_game_logic()) return;
        accumulator += dt;
        if (accumulator < DECAY_INTERVAL) return;
        accumulator -= DECAY_INTERVAL;

        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;
        for (auto& tile : grid->tiles) {
            for (auto& p : tile.pheromone) {
                if (p > 0) p--;
            }
        }
    }
};

// Update per-tile agent density each frame
struct UpdateTileDensitySystem : System<> {
    float stage_log_timer = 0.f;

    void once(float dt) override {
        if (skip_game_logic()) return;
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;

        for (auto& tile : grid->tiles) {
            tile.agent_count = 0;
        }

        auto agents = EntityQuery()
                          .whereHasComponent<Agent>()
                          .whereHasComponent<Transform>()
                          .gen();
        for (Entity& agent : agents) {
            if (!agent.is_missing<BeingServiced>()) continue;
            auto& tf = agent.get<Transform>();
            auto [gx, gz] = grid->world_to_grid(tf.position.x, tf.position.y);
            if (grid->in_bounds(gx, gz)) {
                grid->at(gx, gz).agent_count++;
            }
        }

        stage_log_timer -= dt;
        if (stage_log_timer <= 0.f) {
            stage_log_timer = 5.0f;
            int empty_sf = 0, critical_sf = 0, total_sf = 0;
            int total_sf_agents = 0;
            for (int z = 0; z < MAP_SIZE; z++) {
                for (int x = 0; x < MAP_SIZE; x++) {
                    auto& t = grid->at(x, z);
                    if (t.type != TileType::StageFloor) continue;
                    total_sf++;
                    total_sf_agents += t.agent_count;
                    if (t.agent_count == 0) empty_sf++;
                    float d =
                        t.agent_count / static_cast<float>(MAX_AGENTS_PER_TILE);
                    if (d >= DENSITY_CRITICAL) critical_sf++;
                }
            }
            if (critical_sf > 0) {
                log_warn(
                    "STAGE DENSITY: {}/{} StageFloor tiles empty, "
                    "{} critical, {} total agents on stage",
                    empty_sf, total_sf, critical_sf, total_sf_agents);
            }
        }
    }
};

// Apply crush damage to agents on critically dense tiles.
struct CrushDamageSystem : System<Agent, Transform, AgentHealth> {
    float log_cooldown = 0.f;

    void for_each_with(Entity& e, Agent& agent, Transform& tf,
                       AgentHealth& health, float dt) override {
        if (skip_game_logic()) return;
        if (!e.is_missing<BeingServiced>()) return;

        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;

        auto [gx, gz] = grid->world_to_grid(tf.position.x, tf.position.y);
        if (!grid->in_bounds(gx, gz)) return;

        if (grid->at(gx, gz).type == TileType::MedTent) return;

        int count = grid->at(gx, gz).agent_count;
        float density = count / static_cast<float>(MAX_AGENTS_PER_TILE);
        if (density >= DENSITY_CRITICAL) {
            health.hp -= CRUSH_DAMAGE_RATE * dt;

            log_cooldown -= dt;
            if (log_cooldown <= 0.f) {
                log_cooldown = 2.0f;
                bool watching = !e.is_missing<WatchingStage>();
                bool forcing_flag = agent.is_forcing();
                int min_neighbor = MAX_AGENTS_PER_TILE;
                bool has_empty_walkable = false;
                constexpr int dirs[][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                for (auto [dx, dz] : dirs) {
                    int nx = gx + dx;
                    int nz = gz + dz;
                    if (!grid->in_bounds(nx, nz)) continue;
                    auto& t = grid->at(nx, nz);
                    if (tile_blocks_movement(t.type)) continue;
                    if (t.agent_count < min_neighbor)
                        min_neighbor = t.agent_count;
                    if (t.agent_count == 0) has_empty_walkable = true;
                }
                log_warn(
                    "CRUSH at ({},{}) count={} hp={:.2f} "
                    "watching={} forcing={} stuck={:.1f}s "
                    "flee=({},{}) min_neighbor={} has_empty={}",
                    gx, gz, count, health.hp, watching, forcing_flag,
                    agent.stuck_timer, agent.flee_target_x, agent.flee_target_z,
                    min_neighbor, has_empty_walkable);
            }
        }
    }
};

// Helper: spawn death particles at a world position
static void spawn_death_particles(float wx, float wz, int count, float radius) {
    auto& rng = RandomEngine::get();
    for (int i = 0; i < count; i++) {
        Entity& pe = EntityHelper::createEntity();
        pe.addComponent<Transform>(::vec2{wx, wz});
        pe.addComponent<Particle>();
        auto& p = pe.get<Particle>();

        float angle = rng.get_float(0.f, 6.283f);
        float speed = rng.get_float(radius * 0.5f, radius);
        p.velocity = {std::cos(angle) * speed, std::sin(angle) * speed};
        p.lifetime = rng.get_float(0.3f, 0.5f);
        p.max_lifetime = p.lifetime;
        p.size = rng.get_float(2.f, 4.f);

        if (rng.get_float(0.f, 1.f) > 0.5f) {
            p.color = {255, 80, 60, 255};
        } else {
            p.color = {255, 220, 200, 255};
        }
    }
}

// Remove dead agents, track death count, spawn death particles
struct AgentDeathSystem : System<> {
    void once(float) override {
        if (skip_game_logic()) return;
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();

        struct DeathInfo {
            float wx = 0, wz = 0;
            int count = 0;
        };
        std::unordered_map<int, DeathInfo> deaths_per_tile;

        auto agents = EntityQuery()
                          .whereHasComponent<Agent>()
                          .whereHasComponent<AgentHealth>()
                          .whereHasComponent<Transform>()
                          .gen();
        for (Entity& e : agents) {
            auto& health = e.get<AgentHealth>();
            if (health.hp > 0.f) continue;

            auto& tf = e.get<Transform>();
            int gx = -1, gz = -1;
            if (grid) {
                auto [px, pz] =
                    grid->world_to_grid(tf.position.x, tf.position.y);
                gx = px;
                gz = pz;
            }

            if (gs) {
                gs->death_count++;
                get_audio().play_death();
                log_info("Agent died at ({}, {}), deaths: {}/{}", gx, gz,
                         gs->death_count, gs->max_deaths);
                if (grid && grid->in_bounds(gx, gz)) {
                    int cnt = grid->at(gx, gz).agent_count;
                    constexpr int dirs[][2] = {
                        {1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                    for (auto [ddx, ddz] : dirs) {
                        int nx = gx + ddx;
                        int nz = gz + ddz;
                        if (!grid->in_bounds(nx, nz)) continue;
                        auto& t = grid->at(nx, nz);
                        log_info(
                            "  neighbor ({},{}) type={} count={}{}", nx, nz,
                            static_cast<int>(t.type), t.agent_count,
                            tile_blocks_movement(t.type) ? " BLOCKED" : "");
                    }
                    auto& ag = e.get<Agent>();
                    log_info(
                        "  self: count={} watching={} forcing={} "
                        "stuck={:.1f}s flee=({},{})",
                        cnt, !e.is_missing<WatchingStage>(), ag.is_forcing(),
                        ag.stuck_timer, ag.flee_target_x, ag.flee_target_z);
                }
            }

            int tile_key = gz * MAP_SIZE + gx;
            auto& info = deaths_per_tile[tile_key];
            info.wx = tf.position.x;
            info.wz = tf.position.y;
            info.count++;

            e.cleanup = true;
        }

        for (auto& [key, info] : deaths_per_tile) {
            if (info.count >= 5) {
                spawn_death_particles(info.wx, info.wz, 12, 1.5f);
            } else {
                spawn_death_particles(info.wx, info.wz, 6 * info.count, 0.8f);
            }

            Entity& dme = EntityHelper::createEntity();
            dme.addComponent<DeathMarker>();
            auto& dm = dme.get<DeathMarker>();
            dm.position = {info.wx, info.wz};
        }
        EntityHelper::merge_entity_arrays();

        // Cap death markers at 20 by removing oldest
        auto dm_query = EntityQuery().whereHasComponent<DeathMarker>().gen();
        std::vector<Entity*> markers;
        for (Entity& dme : dm_query) markers.push_back(&dme);
        if (markers.size() > 20) {
            std::sort(markers.begin(), markers.end(), [](Entity* a, Entity* b) {
                return a->get<DeathMarker>().lifetime <
                       b->get<DeathMarker>().lifetime;
            });
            for (size_t i = 0; i < markers.size() - 20; i++)
                markers[i]->cleanup = true;
        }
    }
};

// Move particles and fade alpha; remove when lifetime expires
struct UpdateParticlesSystem : System<Particle, Transform> {
    void for_each_with(Entity& e, Particle& p, Transform& tf,
                       float dt) override {
        if (game_is_paused()) return;
        p.lifetime -= dt;
        if (p.lifetime <= 0.f) {
            e.cleanup = true;
            return;
        }
        tf.position.x += p.velocity.x * dt;
        tf.position.y += p.velocity.y * dt;
        float t = p.lifetime / p.max_lifetime;
        p.color.a = static_cast<unsigned char>(t * 255.f);
    }
};

// Track time_survived and max_attendees; progression
struct TrackStatsSystem : System<> {
    void once(float dt) override {
        if (skip_game_logic()) return;
        auto* gs = EntityHelper::get_singleton_cmp<GameState>();
        if (!gs) return;

        gs->time_survived += dt;

        int count = static_cast<int>(
            EntityQuery().whereHasComponent<Agent>().gen_count());

        int old_max = gs->max_attendees;
        if (count > gs->max_attendees) {
            gs->max_attendees = count;
        }

        auto* fs = EntityHelper::get_singleton_cmp<FacilitySlots>();
        if (fs) {
            int old_slots = fs->get_slots_per_type(old_max);
            int new_slots = fs->get_slots_per_type(gs->max_attendees);
            if (new_slots > old_slots) {
                spawn_toast("New facility slots unlocked!");
                log_info("Progression: {} slots per type (max_attendees={})",
                         new_slots, gs->max_attendees);
            }
        }
    }
};

void register_crowd_flow_systems(SystemManager& sm) {
    sm.register_update_system(std::make_unique<ExodusSystem>());
    sm.register_update_system(std::make_unique<GateExitSystem>());
    sm.register_update_system(std::make_unique<PheromoneDepositSystem>());
    sm.register_update_system(std::make_unique<DecayPheromonesSystem>());
    sm.register_update_system(std::make_unique<UpdateTileDensitySystem>());
}

void register_crowd_damage_systems(SystemManager& sm) {
    sm.register_update_system(std::make_unique<CrushDamageSystem>());
    sm.register_update_system(std::make_unique<AgentDeathSystem>());
    sm.register_update_system(std::make_unique<TrackStatsSystem>());
}

void register_crowd_particle_systems(SystemManager& sm) {
    sm.register_update_system(std::make_unique<UpdateParticlesSystem>());
}
