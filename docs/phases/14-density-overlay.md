# Phase 14: Density Overlay

## Goal
TAB toggles a heat map overlay showing crowd density per tile with smooth gradient.

## Prerequisites
- Phase 01 complete (density tracking)
- Phase 04 complete (basic rendering)

## Deliverables

### 1. Overlay Toggle
- TAB key toggles `GameState::show_data_layer`
- Already exists, but need to improve visualization

### 2. Gradient Colors
Per tile, interpolate color based on density:
```cpp
Color get_density_color(float density_ratio) {
    if (density_ratio < 0.50f) {
        // Transparent to Yellow (0% - 50%)
        float t = density_ratio / 0.50f;
        return Color{255, 255, 0, static_cast<uint8_t>(t * 180)};
    } else if (density_ratio < 0.75f) {
        // Yellow to Orange (50% - 75%)
        float t = (density_ratio - 0.50f) / 0.25f;
        return Color{255, static_cast<uint8_t>(255 - t * 90), 0, 180};
    } else if (density_ratio < 0.90f) {
        // Orange to Red (75% - 90%)
        float t = (density_ratio - 0.75f) / 0.15f;
        return Color{255, static_cast<uint8_t>(165 - t * 165), 0, 200};
    } else {
        // Red to Black (90% - 100%)
        float t = (density_ratio - 0.90f) / 0.10f;
        return Color{static_cast<uint8_t>(255 - t * 255), 0, 0, 220};
    }
}
```

### 3. Smooth Rendering
- Draw colored rectangles over each tile
- Use alpha blending so game is still visible beneath
- Consider smoothing at tile edges (optional)

### 4. Per-Tile Overlay
Only draw on tiles that have agents (or all tiles for uniform look).

## Existing Code to Use

- `GameState::show_data_layer` toggle
- Tile density from Phase 01
- Render systems

## Afterhours to Use

- Color utilities for interpolation (or use manual lerp)

## Implementation Steps

1. Create `RenderDensityOverlaySystem`
2. Only run when `show_data_layer == true`
3. For each tile with agents:
   - Calculate density ratio
   - Get gradient color
   - Draw semi-transparent rectangle
4. Test with crowded areas

## Visual Reference
```
Density:   0%     50%      75%      90%     100%
Color:   Clear → Yellow → Orange → Red → Black
Alpha:    0%      70%      70%      80%     85%
```

## Acceptance Criteria

- [ ] TAB toggles overlay on/off
- [ ] Colors match gradient (yellow → orange → red → black)
- [ ] Overlay is semi-transparent (game visible beneath)
- [ ] Updates in real-time as density changes
- [ ] No performance impact when overlay is off

## Testing

### E2E Test Script: `test_density_overlay.e2e`

```
# Test: Density overlay gradient
reset_game
wait 5

# Toggle overlay on
key TAB
wait 5

screenshot overlay_empty

# Spawn agents at different densities
spawn_agents 0 0 20 stage
spawn_agents 5 0 30 stage
spawn_agents 10 0 38 stage
wait 30

screenshot overlay_gradient_visible

# Toggle overlay off
key TAB
wait 5

screenshot overlay_off
```

Run: `./output/dance.exe --test-mode --test-script="tests/e2e_scripts/test_density_overlay.e2e"`

### Manual Testing

1. Press TAB to toggle overlay
2. Spawn agents in groups
3. Verify color gradient: yellow → orange → red → black
4. Verify overlay is semi-transparent (game visible beneath)
5. Toggle off, verify no performance impact

## Out of Scope
- Colorblind patterns (post-MVP)
- Minimap density display
- Filtering by facility type

