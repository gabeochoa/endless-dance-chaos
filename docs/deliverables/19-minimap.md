# Phase 19: Minimap

**Demo 4D** | Prerequisite: Phase 18 (UI layout exists with sidebar)

## Goal

150x150px minimap at bottom of right sidebar. Shows paths, buildings, fence, gates. Camera viewport rectangle. Click to jump.

**Done when:** Minimap gives useful overview, click-to-jump works.

---

## Deliverables

### 1. Minimap RenderTexture

Create a 150x150 `RenderTexture2D`:

```cpp
struct Minimap : BaseComponent {
    RenderTexture2D texture;
    static constexpr int SIZE = 150;
    // Grid is 52x52 (including fence), so:
    static constexpr float SCALE = 150.0f / 52.0f; // ~2.88 px per tile

    void init() {
        texture = LoadRenderTexture(SIZE, SIZE);
    }
};
```

Render to the texture, then draw the texture into the sidebar UI.

### 2. What to Render

For each tile in the grid, draw a scaled rectangle:

| Tile Type | Minimap Color |
|-----------|---------------|
| Grass | `#98D4A8` (or skip — use as background) |
| Path | `#E8DDD4` |
| Fence | `#555555` |
| Gate | `#4488AA` |
| Stage | `#FFD93D` |
| Bathroom | `#7ECFC0` |
| Food | `#F4A4A4` |

Each tile → ~2.9x2.9 pixel rectangle on the minimap.

### 3. No Agents on Minimap

Agents are NOT shown on the minimap. Use TAB overlay for density visualization (Phase 08).

### 4. Camera Viewport Rectangle

Draw a white outline rectangle showing what the main camera can see:

```cpp
// Calculate viewport in grid coordinates
float view_width_tiles = camera.get_visible_width();   // tiles visible horizontally
float view_height_tiles = camera.get_visible_height();  // tiles visible vertically
float cam_center_x = /* camera target in grid coords */;
float cam_center_z = /* camera target in grid coords */;

// Convert to minimap coordinates
float mm_x = cam_center_x * SCALE;
float mm_y = cam_center_z * SCALE;
float mm_w = view_width_tiles * SCALE;
float mm_h = view_height_tiles * SCALE;

DrawRectangleLinesEx({mm_x - mm_w/2, mm_y - mm_h/2, mm_w, mm_h}, 1, WHITE);
```

Viewport rectangle updates as camera pans, zooms, and rotates.

### 5. Click-to-Jump

When player clicks on the minimap:
- Convert click position within minimap → grid coordinates
- Snap camera to that grid position

```cpp
// minimap_click_x is 0-150 relative to minimap position
float grid_x = minimap_click_x / SCALE;
float grid_z = minimap_click_y / SCALE;
camera.set_target(grid_to_world(grid_x, grid_z));
```

### 6. Drag-to-Pan

When player click-and-drags on minimap:
- Continuously update camera position as mouse moves
- Same conversion as click-to-jump, but every frame while held

### 7. Minimap Placement

In the sidebar (150px wide), minimap goes at the bottom:
- Timeline occupies upper portion
- Minimap occupies bottom 150px
- Use afterhours UI to position the container

### 8. E2E Tests

**`tests/e2e/19_minimap.e2e`**:
```
# Test minimap rendering
reset_game
wait 10

screenshot 19_minimap_default

# Place facilities
place_building bathroom 15 15
place_building food 35 35
wait 10

screenshot 19_minimap_with_buildings

# Draw paths
draw_path_rect 1 24 50 28
wait 10

screenshot 19_minimap_with_paths

# Move camera
key W
key W
key W
wait 10

screenshot 19_minimap_camera_moved
```

---

## Existing Code to Leverage

- Grid data from Phase 01
- Camera system from Phase 01
- Afterhours UI for sidebar placement
- Facility colors from Phase 10

---

## Acceptance Criteria

- [ ] Minimap renders at 150x150px in bottom of sidebar
- [ ] Paths shown on minimap
- [ ] Fence perimeter visible
- [ ] Buildings shown as colored rectangles
- [ ] Camera viewport rectangle visible (white outline)
- [ ] Viewport updates as camera pans/zooms/rotates
- [ ] Click on minimap = instant camera jump
- [ ] Drag on minimap = continuous camera pan
- [ ] No agents on minimap

## Out of Scope

- Agent density on minimap
- Minimap zoom
- Minimap rotation matching camera rotation
