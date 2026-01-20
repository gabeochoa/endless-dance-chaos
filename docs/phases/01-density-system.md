# Phase 01: Density System

## Goal
Track how many agents are in each tile and display the count. This is the foundation for the crush mechanic.

## Prerequisites
- Existing agent movement system works
- PathTile component exists

## Deliverables

### 1. Per-Tile Agent Count
- Add `agent_count` field to `PathTile` component (or create a density tracking map)
- Each frame, count agents within each tile's bounds
- Store count in tile data

### 2. Density Thresholds
Add constants to `game.h`:
```cpp
constexpr int MAX_AGENTS_PER_TILE = 40;
constexpr float DENSITY_WARNING = 0.50f;    // 20 agents
constexpr float DENSITY_DANGEROUS = 0.75f;  // 30 agents
constexpr float DENSITY_CRITICAL = 0.90f;   // 36 agents
```

### 3. Debug Visualization
- When `show_data_layer` is true (TAB key), draw tile density as text
- Color-code tiles: green < 50%, yellow 50-75%, orange 75-90%, red > 90%

## Existing Code to Use

- `PathTile` component in `components.h` — already has `current_load` and `capacity`
- `GameState::show_data_layer` — already toggles with TAB
- Render systems in `render_systems.cpp`

## Afterhours to Use
- None needed for this phase

## Implementation Steps

1. Create a new system `UpdateTileDensitySystem` that:
   - Clears all tile counts each frame
   - Iterates agents, finds their tile, increments count
   
2. Modify render to show density when data layer is on:
   - Draw number on each tile
   - Color based on threshold

## Acceptance Criteria

- [ ] Can see agent count per tile when pressing TAB
- [ ] Colors change based on density thresholds
- [ ] Count updates in real-time as agents move
- [ ] Performance: handles 100+ agents without frame drop

## Testing

### E2E Test Script: `test_density.e2e`

```
# Test: Density tracking
clear_agents
wait 5

# Spawn agents at origin
spawn_agents 0 0 10 stage
wait 10
get_density 0 0
screenshot density_10_agents

# Add more to hit thresholds
spawn_agents 0 0 10 stage
wait 5
screenshot density_20_agents_yellow

spawn_agents 0 0 16 stage
wait 5
screenshot density_36_agents_red
```

Run: `./output/dance.exe --test-mode --test-script="tests/e2e_scripts/test_density.e2e"`

### Manual Testing

1. Run game normally
2. Press TAB to toggle density overlay
3. Watch agents cluster at facilities
4. Verify colors match thresholds:
   - Green: < 20 agents (< 50%)
   - Yellow: 20-30 agents (50-75%)
   - Orange: 30-36 agents (75-90%)
   - Red: > 36 agents (> 90%)

## Out of Scope
- Crush damage (Phase 02)
- Gradient heat map (Phase 14)
- Any UI changes

