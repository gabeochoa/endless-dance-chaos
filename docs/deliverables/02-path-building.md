# Phase 02: Path Building

**Demo 1B** | Prerequisite: Phase 01 (grid renders)

## Goal

Player can draw path rectangles on the grid using mouse input. Paths render visually distinct from grass. Demolish tool removes paths.

**Done when:** You can draw and remove paths on the grid with mouse clicks.

---

## Deliverables

### 1. Path Drawing (Mouse-Only)

Input flow:
1. Left-click to set first corner of rectangle
2. Move mouse — preview shows semi-transparent highlight of affected tiles
3. Left-click again to confirm → all tiles in rectangle become `TileType::Path`
4. Right-click or Escape to cancel pending rectangle

State tracking — add to `BuilderState` or create a new component:

```cpp
struct PathDrawState : BaseComponent {
    bool is_drawing = false;
    int start_x = 0;
    int start_z = 0;
    int hover_x = 0;
    int hover_z = 0;
};
```

### 2. Path Rendering

Paths render as `TileType::Path` tiles in the grid:
- Day color: `#E8DDD4` (light concrete)
- Visually distinct from grass (`#98D4A8`)
- Same isometric projection as grass tiles

### 3. Demolish Mode

Simple toggle:
- Press `D` key (or right-click) to switch to demolish mode
- Click on a path tile → sets it back to `TileType::Grass`
- Press `D` again or `Escape` to exit demolish mode
- Visual indicator: cursor turns red when in demolish mode

### 4. Preview Rendering

While drawing:
- Highlight rectangle between start corner and current cursor position
- Valid tiles: green semi-transparent overlay
- Already-path tiles within selection: no change indicator (gray)

### 5. Mouse-to-Grid Conversion

Create helper to convert screen mouse position to grid coordinates:

```cpp
// Uses IsometricCamera to unproject screen pos → world pos → grid coords
std::optional<std::pair<int,int>> screen_to_grid(
    const IsometricCamera& cam, float screen_x, float screen_y);
```

This is critical — isometric projection means mouse position needs proper unprojection through the camera.

### 6. E2E Commands

| Command | Args | Description |
|---------|------|-------------|
| `set_tile` | `X Z TYPE` | Directly set tile type (grass/path/fence/etc) |
| `draw_path_rect` | `X1 Z1 X2 Z2` | Draw path rectangle programmatically |

**`tests/e2e/02_path_drawing.e2e`**:
```
# Test path drawing
reset_game
wait 5

# Draw a path rectangle via command
draw_path_rect 10 10 15 15
wait 5
assert_tile_type 12 12 path
assert_tile_type 10 10 path
assert_tile_type 9 9 grass
screenshot 02_path_drawn

# Remove a tile
set_tile 12 12 grass
wait 5
assert_tile_type 12 12 grass
screenshot 02_path_removed
```

---

## Existing Code to Leverage

- `Grid` singleton from Phase 01
- `IsometricCamera` for screen-to-world projection
- `input_mapping.h` for input actions

## What to Delete

- Old `BuilderState::pending_tiles` flow — replaced by rectangle drag on grid

---

## Acceptance Criteria

- [ ] Left-click starts rectangle, second left-click fills with paths
- [ ] Preview highlights tiles during draw
- [ ] Right-click / Escape cancels pending rectangle
- [ ] Demolish mode (D key) removes paths on click
- [ ] Paths are visually distinct from grass
- [ ] Mouse-to-grid conversion works correctly at all 4 camera rotations
- [ ] E2E: `draw_path_rect` and `assert_tile_type` work

## Out of Scope

- Controller input (comes in Phase 16 / Demo 4A)
- Fence/gate building (Phase 11 / Demo 2A)
- Multiple build tools (Phase 16)
- Path cost for pathfinding
