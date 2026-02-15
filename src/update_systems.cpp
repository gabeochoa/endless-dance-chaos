// AFTER_HOURS_REPLACE_LOGGING and log.h must come first
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "components.h"
#include "entity_makers.h"
#include "systems.h"

struct CameraInputSystem : System<ProvidesCamera> {
    void for_each_with(Entity&, ProvidesCamera& cam, float dt) override {
        cam.cam.handle_input(dt);
    }
};

// Handle path drawing (rectangle drag) and demolish mode
struct PathBuildSystem : System<> {
    void once(float) override {
        auto* pds = EntityHelper::get_singleton_cmp<PathDrawState>();
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!pds || !grid) return;

        // Toggle demolish mode with X key
        if (action_pressed(InputAction::ToggleDemolish)) {
            pds->demolish_mode = !pds->demolish_mode;
            pds->is_drawing = false;  // Cancel any pending draw
            log_info("Demolish mode: {}", pds->demolish_mode ? "ON" : "OFF");
        }

        // Cancel with Escape or right-click
        if (action_pressed(InputAction::Cancel) ||
            input::is_mouse_button_pressed(raylib::MOUSE_BUTTON_RIGHT)) {
            if (pds->is_drawing) {
                pds->is_drawing = false;
            } else if (pds->demolish_mode) {
                pds->demolish_mode = false;
            }
            return;
        }

        if (!pds->hover_valid) return;

        // Left-click actions
        if (input::is_mouse_button_pressed(raylib::MOUSE_BUTTON_LEFT)) {
            if (pds->demolish_mode) {
                // Demolish: click on path -> revert to grass
                Tile& tile = grid->at(pds->hover_x, pds->hover_z);
                if (tile.type == TileType::Path) {
                    tile.type = TileType::Grass;
                }
            } else if (!pds->is_drawing) {
                // Start rectangle: set first corner
                pds->start_x = pds->hover_x;
                pds->start_z = pds->hover_z;
                pds->is_drawing = true;
            } else {
                // Confirm rectangle: fill all tiles with path
                int min_x, min_z, max_x, max_z;
                pds->get_rect(min_x, min_z, max_x, max_z);

                for (int z = min_z; z <= max_z; z++) {
                    for (int x = min_x; x <= max_x; x++) {
                        if (grid->in_bounds(x, z)) {
                            grid->at(x, z).type = TileType::Path;
                        }
                    }
                }
                pds->is_drawing = false;
            }
        }
    }
};

// Greedy neighbor pathfinding: pick the adjacent tile closest to goal,
// preferring Path tiles over Grass when distances are equal.
static std::pair<int, int> pick_next_tile(int cur_x, int cur_z, int goal_x,
                                          int goal_z, const Grid& grid) {
    static constexpr int dx[] = {1, -1, 0, 0};
    static constexpr int dz[] = {0, 0, 1, -1};

    int best_x = cur_x;
    int best_z = cur_z;
    int best_dist = std::abs(cur_x - goal_x) + std::abs(cur_z - goal_z);
    bool best_is_path = false;

    for (int i = 0; i < 4; i++) {
        int nx = cur_x + dx[i];
        int nz = cur_z + dz[i];
        if (!grid.in_bounds(nx, nz)) continue;

        TileType type = grid.at(nx, nz).type;
        // Can't walk through fences
        if (type == TileType::Fence) continue;

        int dist = std::abs(nx - goal_x) + std::abs(nz - goal_z);
        bool is_path = (type == TileType::Path);

        // Pick if closer, or same distance but on path and current best isn't
        if (dist < best_dist ||
            (dist == best_dist && is_path && !best_is_path)) {
            best_x = nx;
            best_z = nz;
            best_dist = dist;
            best_is_path = is_path;
        }
    }

    return {best_x, best_z};
}

// Move agents toward their target using greedy pathfinding
struct AgentMovementSystem : System<Agent, Transform> {
    void for_each_with(Entity&, Agent& agent, Transform& tf,
                       float dt) override {
        if (agent.target_grid_x < 0 || agent.target_grid_z < 0) return;

        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        if (!grid) return;

        // Get current grid position
        auto [cur_gx, cur_gz] =
            grid->world_to_grid(tf.position.x, tf.position.y);

        // Already at target?
        if (cur_gx == agent.target_grid_x && cur_gz == agent.target_grid_z)
            return;

        // Adjust speed based on current terrain
        TileType cur_type = TileType::Grass;
        if (grid->in_bounds(cur_gx, cur_gz)) {
            cur_type = grid->at(cur_gx, cur_gz).type;
        }
        agent.speed = (cur_type == TileType::Path) ? SPEED_PATH : SPEED_GRASS;

        // Pick next tile via greedy neighbor
        auto [next_x, next_z] = pick_next_tile(
            cur_gx, cur_gz, agent.target_grid_x, agent.target_grid_z, *grid);

        // Move toward center of next tile
        ::vec2 target_world = grid->grid_to_world(next_x, next_z);
        float dx = target_world.x - tf.position.x;
        float dz = target_world.y - tf.position.y;  // vec2.y = world z
        float dist = std::sqrt(dx * dx + dz * dz);

        if (dist > 0.01f) {
            float step = agent.speed * TILESIZE * dt;
            if (step > dist) step = dist;
            tf.position.x += (dx / dist) * step;
            tf.position.y += (dz / dist) * step;
        }
    }
};

// Spawn agents at the spawn point on a timer
struct SpawnAgentSystem : System<> {
    void once(float dt) override {
        auto* ss = EntityHelper::get_singleton_cmp<SpawnState>();
        if (!ss || !ss->enabled) return;

        ss->timer += dt;
        if (ss->timer >= ss->interval) {
            ss->timer -= ss->interval;

            // Spawn agent heading toward the stage center
            int stage_center_x = STAGE_X + STAGE_SIZE / 2;
            int stage_center_z = STAGE_Z + STAGE_SIZE / 2;
            make_agent(SPAWN_X, SPAWN_Z, FacilityType::Stage, stage_center_x,
                       stage_center_z);
        }
    }
};

void register_update_systems(SystemManager& sm) {
    sm.register_update_system(std::make_unique<CameraInputSystem>());
    sm.register_update_system(std::make_unique<PathBuildSystem>());
    sm.register_update_system(std::make_unique<AgentMovementSystem>());
    sm.register_update_system(std::make_unique<SpawnAgentSystem>());
}
