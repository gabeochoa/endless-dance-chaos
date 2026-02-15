# Phase 16: Full Build Tools

**Demo 4A** | Prerequisite: Phase 15 (Demo 3 complete)

## Goal

All 7 build tools working with controller-friendly cycling. Rectangle drag for paths/fences, point placement for facilities, demolish mode. Try pickup-and-move for facilities.

**Done when:** All building tools work, placement feels responsive and clear.

---

## Deliverables

### 1. Build Tool Enum (Extended)

```cpp
enum class BuildTool {
    Path,      // Rectangle drag → fills with TileType::Path
    Fence,     // Rectangle drag → fills with TileType::Fence
    Gate,      // Point place → 2x1 TileType::Gate
    Stage,     // Point place → 4x4 TileType::Stage (limited by slots)
    Bathroom,  // Point place → 2x2 TileType::Bathroom (limited by slots)
    Food,      // Point place → 2x2 TileType::Food (limited by slots)
    Demolish   // Click to remove
};
```

### 2. Tool Cycling

- `[` key (L bumper): Previous tool
- `]` key (R bumper): Next tool
- Number keys 1-7: Direct tool select
- Current tool tracked in `BuilderState`

### 3. Rectangle Drag (Path & Fence)

Same mechanic as Phase 02 path drawing, extended to fences:
1. Left-click to set first corner
2. Move cursor — preview shows rectangle
3. Left-click to confirm → fill tiles with selected type
4. Right-click or Escape to cancel

### 4. Point Placement (Gate, Stage, Bathroom, Food)

1. Move cursor over valid location
2. Preview shows footprint:
   - Gate: 2x1
   - Stage: 4x4
   - Bathroom: 2x2
   - Food: 2x2
3. Green preview = valid, red preview = invalid (occupied, out of bounds)
4. Left-click to place
5. Facility slot check: `can_place()` based on progression (Phase 17)

### 5. Cursor Highlight

- Highlighted tile(s) with semi-transparent fill
- Green: valid placement
- Red: invalid (tile occupied by another facility/fence, or out of playable area)
- No text feedback — just color

### 6. Demolish Mode

When demolish tool selected:
- Cursor shows X indicator
- Left-click removes placed object:
  - Path → Grass
  - Fence → Grass (except perimeter)
  - Facility → Grass (returns slot)
- **Cannot demolish last gate** — check `count_gates() > 1` before allowing

### 7. Facility Pickup and Move (Try First)

When cursor is over an existing facility:
- Press a "grab" key (e.g., G or right-click):
  - Facility detaches from grid (tiles become Grass)
  - Facility follows cursor as preview
  - Left-click to place at new location
  - Escape to cancel (facility returns to original position)
- If this doesn't feel right during testing, fall back to demolish + rebuild

### 8. Placement Validation

```cpp
bool can_place_at(const Grid& grid, int x, int z, int width, int height) {
    for (int dx = 0; dx < width; dx++) {
        for (int dz = 0; dz < height; dz++) {
            int tx = x + dx, tz = z + dz;
            if (!grid.in_bounds(tx, tz)) return false;
            if (tx < Grid::PLAY_MIN || tx > Grid::PLAY_MAX) return false;
            if (tz < Grid::PLAY_MIN || tz > Grid::PLAY_MAX) return false;
            TileType t = grid.at(tx, tz).type;
            if (t != TileType::Grass && t != TileType::Path) return false;
        }
    }
    return true;
}
```

### 9. Controller Mapping

| Input | Action |
|-------|--------|
| D-pad / Left Stick / Mouse | Move cursor |
| A / Left-click | Place / Confirm |
| B / Right-click / Escape | Cancel / Delete pending |
| L Bumper / `[` | Previous tool |
| R Bumper / `]` | Next tool |
| 1-7 | Direct tool select |

### 10. E2E Commands

| Command | Args | Description |
|---------|------|-------------|
| `select_tool` | `TOOL` | Select build tool (path/fence/gate/stage/bathroom/food/demolish) |
| `assert_tool` | `TOOL` | Assert current tool |
| `place_building` | `TYPE X Z` | Place a building at grid pos |
| `demolish_at` | `X Z` | Demolish at grid pos |

**`tests/e2e/16_build_tools.e2e`**:
```
# Test tool cycling
reset_game
wait 5

select_tool path
assert_tool path

select_tool fence
assert_tool fence

select_tool gate
assert_tool gate

# Place a gate
place_building gate 50 26
wait 5
assert_tile_type 50 26 gate
screenshot 16_gate_placed

# Place a bathroom
place_building bathroom 30 30
wait 5
assert_tile_type 30 30 bathroom
screenshot 16_bathroom_placed

# Demolish
demolish_at 30 30
wait 5
assert_tile_type 30 30 grass
screenshot 16_demolished
```

**`tests/e2e/16_cant_delete_last_gate.e2e`**:
```
# Test last gate protection
reset_game
wait 5

# Only one gate exists
assert_gate_count eq 1

# Try to demolish it - should fail
demolish_at 0 26
wait 5

# Gate should still exist
assert_gate_count eq 1
screenshot 16_gate_protected
```

---

## Existing Code to Leverage

- `BuilderState` component (extend with new tools)
- `PathDrawState` from Phase 02 (extend for fence)
- Input mapping system
- Grid from Phase 01

---

## Acceptance Criteria

- [ ] All 7 tools selectable via cycling or number keys
- [ ] Path and fence use rectangle drag
- [ ] Facilities use point placement with footprint preview
- [ ] Green/red preview indicates validity
- [ ] Demolish removes placed objects
- [ ] Cannot demolish last gate
- [ ] Facility pickup-and-move works (or documented fallback to demolish+rebuild)
- [ ] Controller mapping matches spec

## Out of Scope

- Build bar UI display (Phase 18)
- Facility slot limits (Phase 17)
- Cost system
