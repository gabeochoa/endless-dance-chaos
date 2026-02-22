// Building domain: path/fence/facility placement and demolition.
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "audio.h"
#include "components.h"
#include "systems.h"
#include "update_helpers.h"

// Check if all tiles in a footprint are valid for placement
static bool can_place_at(const Grid& grid, int x, int z, int w, int h) {
    for (int dz = 0; dz < h; dz++) {
        for (int dx = 0; dx < w; dx++) {
            int tx = x + dx, tz = z + dz;
            if (!grid.in_playable(tx, tz)) return false;
            TileType t = grid.at(tx, tz).type;
            if (t != TileType::Grass && t != TileType::Path &&
                t != TileType::StageFloor)
                return false;
        }
    }
    return true;
}

// Handle all build tools: rect drag for path/fence, point for facilities
struct PathBuildSystem : System<> {
    void once(float) override {
        if (game_is_over()) return;
        auto* pds = EntityHelper::get_singleton_cmp<PathDrawState>();
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        auto* bs = EntityHelper::get_singleton_cmp<BuilderState>();
        if (!pds || !grid || !bs) return;

        // Tool cycling with [ ]
        if (action_pressed(InputAction::PrevTool)) {
            int t = static_cast<int>(bs->tool);
            t = (t + 7) % 8;
            bs->tool = static_cast<BuildTool>(t);
            pds->is_drawing = false;
        }
        if (action_pressed(InputAction::NextTool)) {
            int t = static_cast<int>(bs->tool);
            t = (t + 1) % 8;
            bs->tool = static_cast<BuildTool>(t);
            pds->is_drawing = false;
        }
        // Direct select
        if (action_pressed(InputAction::ToolPath)) bs->tool = BuildTool::Path;
        if (action_pressed(InputAction::ToolFence)) bs->tool = BuildTool::Fence;
        if (action_pressed(InputAction::ToolGate)) bs->tool = BuildTool::Gate;
        if (action_pressed(InputAction::ToolStage)) bs->tool = BuildTool::Stage;
        if (action_pressed(InputAction::Tool5)) bs->tool = BuildTool::Bathroom;
        if (action_pressed(InputAction::Tool6)) bs->tool = BuildTool::Food;
        if (action_pressed(InputAction::Tool7)) bs->tool = BuildTool::MedTent;
        if (action_pressed(InputAction::Tool8)) bs->tool = BuildTool::Demolish;
        if (action_pressed(InputAction::ToggleDemolish))
            bs->tool = BuildTool::Demolish;

        if (action_pressed(InputAction::Cancel) ||
            input::is_mouse_button_pressed(MOUSE_BUTTON_RIGHT)) {
            pds->is_drawing = false;
            return;
        }

        if (!pds->hover_valid) return;

        if (input::is_mouse_button_pressed(MOUSE_BUTTON_LEFT)) {
            int hx = pds->hover_x, hz = pds->hover_z;

            switch (bs->tool) {
                case BuildTool::Path:
                case BuildTool::Fence: {
                    if (!pds->is_drawing) {
                        pds->start_x = hx;
                        pds->start_z = hz;
                        pds->is_drawing = true;
                    } else {
                        int min_x, min_z, max_x, max_z;
                        pds->get_rect(min_x, min_z, max_x, max_z);
                        TileType fill = (bs->tool == BuildTool::Path)
                                            ? TileType::Path
                                            : TileType::Fence;
                        for (int z = min_z; z <= max_z; z++) {
                            for (int x = min_x; x <= max_x; x++) {
                                if (!grid->in_bounds(x, z)) continue;
                                if (grid->at(x, z).type != TileType::Grass)
                                    continue;
                                grid->at(x, z).type = fill;
                            }
                        }
                        pds->is_drawing = false;
                        grid->mark_tiles_dirty();
                        get_audio().play_place();
                    }
                    break;
                }
                case BuildTool::Gate: {
                    if (can_place_at(*grid, hx, hz, 1, 2)) {
                        grid->place_footprint(hx, hz, 1, 2, TileType::Gate);
                        grid->rebuild_gate_cache();
                        get_audio().play_place();
                    }
                    break;
                }
                case BuildTool::Stage: {
                    if (can_place_at(*grid, hx, hz, 4, 4)) {
                        grid->place_footprint(hx, hz, 4, 4, TileType::Stage);
                        get_audio().play_place();
                    }
                    break;
                }
                case BuildTool::Bathroom: {
                    if (can_place_at(*grid, hx, hz, 2, 2)) {
                        grid->place_footprint(hx, hz, 2, 2, TileType::Bathroom);
                        get_audio().play_place();
                    }
                    break;
                }
                case BuildTool::Food: {
                    if (can_place_at(*grid, hx, hz, 2, 2)) {
                        grid->place_footprint(hx, hz, 2, 2, TileType::Food);
                        get_audio().play_place();
                    }
                    break;
                }
                case BuildTool::MedTent: {
                    if (can_place_at(*grid, hx, hz, 2, 2)) {
                        grid->place_footprint(hx, hz, 2, 2, TileType::MedTent);
                        get_audio().play_place();
                    }
                    break;
                }
                case BuildTool::Demolish: {
                    Tile& tile = grid->at(hx, hz);
                    if (tile.type == TileType::Path ||
                        tile.type == TileType::Fence ||
                        tile.type == TileType::Bathroom ||
                        tile.type == TileType::Food ||
                        tile.type == TileType::MedTent ||
                        tile.type == TileType::Stage) {
                        bool is_gate = (tile.type == TileType::Gate);
                        if (is_gate && grid->gate_count() <= 1) break;
                        tile.type = TileType::Grass;
                        grid->mark_tiles_dirty();
                        get_audio().play_demolish();
                    }
                    break;
                }
            }
        }
    }
};

void register_building_systems(SystemManager& sm) {
    sm.register_update_system(std::make_unique<PathBuildSystem>());
}
