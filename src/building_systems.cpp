// Building domain: path/fence/facility placement and demolition.
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "audio.h"
#include "components.h"
#include "render_helpers.h"
#include "systems.h"
#include "update_helpers.h"

// Check if all tiles in a footprint are valid for placement
static bool can_place_at(const Grid& grid, int x, int z, int w, int h) {
    for (int dz = 0; dz < h; dz++) {
        for (int dx = 0; dx < w; dx++) {
            int tx = x + dx, tz = z + dz;
            if (!grid.in_playable(tx, tz)) return false;
            TileType t = grid.at(tx, tz).type;
            if (t != TileType::Grass && !is_path_tile(t) &&
                t != TileType::StageFloor)
                return false;
        }
    }
    return true;
}

struct PlacementMeta {
    TileType tile_type;
    int width, height;
};

// Display names indexed by BuildTool, for the insufficient-funds toast/log.
static constexpr const char* TOOL_INFO_NAMES[] = {
    "Path",     "Fence",         "Gate",          "Stage",
    "Bathroom", "Food",          "Med Tent",      "Demolish",
    "Bar",      "Security Post", "Water Station", "Tree",
    "Bench",    "Dirt Path",     "Neon Path",     "Lamp",
};

static const PlacementMeta* get_placement_meta(BuildTool tool) {
    static constexpr PlacementMeta TABLE[] = {
        {TileType::Gate, 1, 2},         {TileType::Stage, 4, 4},
        {TileType::Bathroom, 2, 2},     {TileType::Food, 2, 2},
        {TileType::MedTent, 2, 2},      {TileType::Bar, 2, 2},
        {TileType::SecurityPost, 1, 1}, {TileType::WaterStation, 1, 1},
        {TileType::Tree, 1, 1},         {TileType::Bench, 1, 1},
        {TileType::Lamp, 1, 1},
    };
    switch (tool) {
        case BuildTool::Gate:
            return &TABLE[0];
        case BuildTool::Stage:
            return &TABLE[1];
        case BuildTool::Bathroom:
            return &TABLE[2];
        case BuildTool::Food:
            return &TABLE[3];
        case BuildTool::MedTent:
            return &TABLE[4];
        case BuildTool::Bar:
            return &TABLE[5];
        case BuildTool::SecurityPost:
            return &TABLE[6];
        case BuildTool::WaterStation:
            return &TABLE[7];
        case BuildTool::Tree:
            return &TABLE[8];
        case BuildTool::Bench:
            return &TABLE[9];
        case BuildTool::Lamp:
            return &TABLE[10];
        default:
            return nullptr;
    }
}

// Handle all build tools: rect drag for path/fence, point for facilities
struct PathBuildSystem : System<> {
    void once(float) override {
        if (game_is_over()) return;
        auto* pds = EntityHelper::get_singleton_cmp<PathDrawState>();
        auto* grid = EntityHelper::get_singleton_cmp<Grid>();
        auto* bs = EntityHelper::get_singleton_cmp<BuilderState>();
        if (!pds || !grid || !bs) return;

        // Tool cycling with [ ] — 16 tools total
        if (action_pressed(InputAction::PrevTool)) {
            int t = static_cast<int>(bs->tool);
            t = (t + 15) % 16;
            bs->tool = static_cast<BuildTool>(t);
            pds->is_drawing = false;
        }
        if (action_pressed(InputAction::NextTool)) {
            int t = static_cast<int>(bs->tool);
            t = (t + 1) % 16;
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
        if (action_pressed(InputAction::Tool9)) bs->tool = BuildTool::Bar;
        if (action_pressed(InputAction::Tool10))
            bs->tool = BuildTool::SecurityPost;
        if (action_pressed(InputAction::Tool11))
            bs->tool = BuildTool::WaterStation;
        if (action_pressed(InputAction::ToggleDemolish))
            bs->tool = BuildTool::Demolish;

        if (action_pressed(InputAction::Cancel) ||
            input::is_mouse_button_pressed(MOUSE_BUTTON_RIGHT)) {
            pds->is_drawing = false;
            return;
        }

        // A press must act where the mouse IS, not where hover was
        // last frame. E2E `click` teleports the mouse and presses
        // atomically, and this system (update) runs before
        // HoverTracking (render) refreshes hover — without this, a
        // UI click with a build tool active starts a drag / places
        // a building at the stale hover tile. That is how 17's
        // ISO-toggle clicks leaked a drag that later broke grounds'
        // drag (the finish rect was computed from the stale start).
        // click_grid is unaffected: it moves the mouse to the same
        // tile whose hover it injects, so the refresh agrees.
        if (input::is_mouse_button_pressed(MOUSE_BUTTON_LEFT)) {
            auto mouse = input::get_mouse_position();
            if (mouse_over_ui(mouse.x, mouse.y)) {
                pds->hover_valid = false;
            } else {
                auto* cam = EntityHelper::get_singleton_cmp<ProvidesCamera>();
                auto result = cam ? cam->cam.screen_to_grid(mouse.x, mouse.y)
                                  : std::nullopt;
                if (result.has_value()) {
                    auto [gx, gz] = result.value();
                    pds->hover_x = gx;
                    pds->hover_z = gz;
                    pds->hover_valid = grid->in_bounds(gx, gz);
                } else {
                    pds->hover_valid = false;
                }
            }
        }

        if (!pds->hover_valid) return;

        if (input::is_mouse_button_pressed(MOUSE_BUTTON_LEFT)) {
            int hx = pds->hover_x, hz = pds->hover_z;

            switch (bs->tool) {
                case BuildTool::Path:
                case BuildTool::DirtPath:
                case BuildTool::NeonPath:
                case BuildTool::Fence: {
                    if (!pds->is_drawing) {
                        pds->start_x = hx;
                        pds->start_z = hz;
                        pds->is_drawing = true;
                    } else {
                        int min_x, min_z, max_x, max_z;
                        pds->get_rect(min_x, min_z, max_x, max_z);
                        TileType fill = (bs->tool == BuildTool::Fence)
                                            ? TileType::Fence
                                        : bs->tool == BuildTool::DirtPath
                                            ? TileType::DirtPath
                                        : bs->tool == BuildTool::NeonPath
                                            ? TileType::NeonPath
                                            : TileType::Path;
                        // Paths charge per newly converted tile (only
                        // Grass tiles convert); the whole rect is
                        // refused when unaffordable, like facilities.
                        // Fence stays free.
                        if (is_path_tile(fill)) {
                            const PathTypeInfo* info = path_info_for(fill);
                            int converts = 0;
                            for (int z = min_z; z <= max_z; z++)
                                for (int x = min_x; x <= max_x; x++)
                                    if (grid->in_bounds(x, z) &&
                                        grid->at(x, z).type == TileType::Grass)
                                        converts++;
                            int cost =
                                converts * (info ? info->cost_per_tile : 0);
                            auto* econ =
                                EntityHelper::get_singleton_cmp<Economy>();
                            if (econ && !econ->try_spend(cost)) {
                                spawn_toast(fmt::format(
                                    "Not enough cash! {} path costs ${}",
                                    TOOL_INFO_NAMES[static_cast<int>(bs->tool)],
                                    cost));
                                log_warn(
                                    "Build refused: {} path costs ${}, cash "
                                    "is ${}",
                                    TOOL_INFO_NAMES[static_cast<int>(bs->tool)],
                                    cost, econ->cash);
                                pds->is_drawing = false;
                                break;
                            }
                        }
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
                case BuildTool::Gate:
                case BuildTool::Stage:
                case BuildTool::Bathroom:
                case BuildTool::Food:
                case BuildTool::MedTent:
                case BuildTool::Bar:
                case BuildTool::SecurityPost:
                case BuildTool::WaterStation:
                case BuildTool::Tree:
                case BuildTool::Bench:
                case BuildTool::Lamp: {
                    auto* meta = get_placement_meta(bs->tool);
                    if (meta && can_place_at(*grid, hx, hz, meta->width,
                                             meta->height)) {
                        int cost = build_cost_for_tool(bs->tool);
                        auto* econ = EntityHelper::get_singleton_cmp<Economy>();
                        if (econ && !econ->try_spend(cost)) {
                            spawn_toast(fmt::format(
                                "Not enough cash! {} costs ${}",
                                TOOL_INFO_NAMES[static_cast<int>(bs->tool)],
                                cost));
                            log_warn(
                                "Build refused: {} costs ${}, cash is ${}",
                                TOOL_INFO_NAMES[static_cast<int>(bs->tool)],
                                cost, econ->cash);
                            break;
                        }
                        grid->place_footprint(hx, hz, meta->width, meta->height,
                                              meta->tile_type);
                        if (meta->tile_type == TileType::Gate)
                            grid->rebuild_gate_cache();
                        get_audio().play_place();
                    }
                    break;
                }
                case BuildTool::Demolish: {
                    Tile& tile = grid->at(hx, hz);
                    if (is_path_tile(tile.type) ||
                        tile.type == TileType::Fence ||
                        tile.type == TileType::Bathroom ||
                        tile.type == TileType::Food ||
                        tile.type == TileType::MedTent ||
                        tile.type == TileType::Bar ||
                        tile.type == TileType::SecurityPost ||
                        tile.type == TileType::WaterStation ||
                        tile.type == TileType::Tree ||
                        tile.type == TileType::Bench ||
                        tile.type == TileType::Lamp ||
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
