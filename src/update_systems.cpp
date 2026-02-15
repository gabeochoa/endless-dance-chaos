// AFTER_HOURS_REPLACE_LOGGING and log.h must come first
#define AFTER_HOURS_REPLACE_LOGGING
#include "log.h"

#include "afterhours/src/core/entity_helper.h"
#include "components.h"
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

void register_update_systems(SystemManager& sm) {
    sm.register_update_system(std::make_unique<CameraInputSystem>());
    sm.register_update_system(std::make_unique<PathBuildSystem>());
}
