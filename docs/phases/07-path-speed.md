# Phase 07: Path Speed Differential

## Goal
Agents move at full speed (0.5 tiles/sec) on paths, half speed (0.25 tiles/sec) on grass.

## Prerequisites
- Phase 06 complete (fence/gate system)

## Deliverables

### 1. Speed Constants
Add to `game.h`:
```cpp
constexpr float AGENT_SPEED_PATH = 0.5f;   // tiles/sec
constexpr float AGENT_SPEED_GRASS = 0.25f; // tiles/sec (50%)
```

### 2. Terrain Check
Function to check if position is on path:
```cpp
bool is_on_path(vec2 position) {
    // Convert world position to grid coordinates
    int grid_x = static_cast<int>(std::floor(position.x / TILESIZE));
    int grid_z = static_cast<int>(std::floor(position.y / TILESIZE));
    
    // Check if PathTile exists at this grid position
    return path_tile_exists_at(grid_x, grid_z);
}
```

### 3. Speed Modifier in Movement
In agent movement system:
```cpp
float base_speed = is_on_path(agent_pos) ? AGENT_SPEED_PATH : AGENT_SPEED_GRASS;
// Also apply dangerous zone slowdown from Phase 01
float density_modifier = get_density_slowdown(tile_density);
float final_speed = base_speed * density_modifier;
```

### 4. Visual Differentiation
Ensure paths are visually distinct from grass:
- Paths: Light concrete color (#E8DDD4 day / #2A2A3A night)
- Grass: Mint green (#98D4A8 day / #2D4A3E night)

## Existing Code to Use

- `PathTile` component and grid lookup
- Agent velocity/movement in `update_systems.cpp`
- Color constants can go in a new `colors.h`

## Afterhours to Use
- None needed

## Implementation Steps

1. Add speed constants to game.h
2. Create helper function `is_on_path(vec2)` or `get_terrain_at(grid_x, grid_z)`
3. Modify agent movement to query terrain
4. Apply speed based on terrain type
5. Verify visual distinction between path and grass

## Acceptance Criteria

- [ ] Agents visibly move faster on paths
- [ ] Agents slow down on grass
- [ ] Speed difference is 2x (0.5 vs 0.25)
- [ ] Paths are visually distinct from grass
- [ ] No jittering at path/grass boundaries

## Out of Scope
- Path building (Phase 08)
- Path cost for pathfinding
- Plaza merging

