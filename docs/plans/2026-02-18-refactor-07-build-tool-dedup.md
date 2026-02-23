# Refactor 07: Deduplicate Build Tool Placement Logic

## Problem

In `PathBuildSystem::once()` (`building_systems.cpp`), the facility placement cases for `Bathroom`, `Food`, and `MedTent` are identical except for the `TileType`:

```cpp
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
```

The `Stage` case follows the same pattern with a 4x4 footprint. `Gate` uses 1x2.

## Proposed Fix

### Add a build tool metadata table

```cpp
struct BuildToolMeta {
    TileType tile_type;
    int width;
    int height;
};

static constexpr BuildToolMeta FACILITY_META[] = {
    {TileType::Gate,     1, 2},  // BuildTool::Gate
    {TileType::Stage,    4, 4},  // BuildTool::Stage
    {TileType::Bathroom, 2, 2},  // BuildTool::Bathroom
    {TileType::Food,     2, 2},  // BuildTool::Food
    {TileType::MedTent,  2, 2},  // BuildTool::MedTent
};
```

### Collapse facility placement cases

```cpp
case BuildTool::Gate:
case BuildTool::Stage:
case BuildTool::Bathroom:
case BuildTool::Food:
case BuildTool::MedTent: {
    auto& meta = FACILITY_META[static_cast<int>(bs->tool) - static_cast<int>(BuildTool::Gate)];
    if (can_place_at(*grid, hx, hz, meta.width, meta.height)) {
        grid->place_footprint(hx, hz, meta.width, meta.height, meta.tile_type);
        if (meta.tile_type == TileType::Gate) grid->rebuild_gate_cache();
        get_audio().play_place();
    }
    break;
}
```

This reduces 5 near-identical cases to 1.

## Files to change

- `src/building_systems.cpp` — Add `BuildToolMeta` table, collapse switch cases

## Estimated impact

- **Performance:** None — same runtime behavior.
- **Complexity:** Removes ~25 lines of duplicated code. Adding a new facility type requires only a table entry instead of a new switch case. Eliminates the risk of updating one case but not the others.

## Risks

- The `Gate` case has extra logic (`rebuild_gate_cache`). This is handled with a conditional in the collapsed case. If more tool-specific logic accumulates, the table approach may need per-tool hooks.
- `Path` and `Fence` use rectangle-drag logic (two-click flow) and `Demolish` has different validation, so those remain as separate cases. The table only applies to single-click placement tools.
