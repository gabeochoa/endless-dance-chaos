# Refactor 01: Cull Grid Rendering to Visible Tiles & Merge Overlay Passes

## Problem

`RenderGridSystem` draws all 2,704 tiles (52x52) every frame as individual `draw_plane` calls, regardless of what's visible through the camera. `RenderDensityFlashSystem` and `RenderDensityOverlaySystem` also iterate the full grid independently — three full-grid loops in `render_world.cpp` alone.

### Current code paths

- `RenderGridSystem::once()` — 2,704 `draw_plane` calls per frame
- `RenderDensityFlashSystem::once()` — 2,704 tile reads, draws for tiles above danger threshold
- `RenderDensityOverlaySystem::once()` — 2,704 tile reads (guarded by `show_data_layer`), draws for tiles with agents

With an orthographic isometric camera, typically only 20–40% of the grid is visible at any zoom level.

## Proposed Fix

### Part A: Viewport culling

1. At the start of each render frame, compute a visible tile AABB from the camera:
   - Project the four screen corners to grid coordinates using `screen_to_grid()` (already exists on `IsometricCamera`).
   - Compute `min_x, max_x, min_z, max_z` with a small margin (e.g., +2 tiles).
   - Clamp to `[0, MAP_SIZE)`.

2. Replace the `for z in 0..MAP_SIZE, x in 0..MAP_SIZE` loops in all three systems with `for z in min_z..max_z, x in min_x..max_x`.

3. Store the visible AABB in a lightweight struct (e.g., `VisibleRegion`) computed once per frame in `BeginRenderSystem` and read by subsequent systems.

### Part B: Merge density overlays

Combine `RenderDensityFlashSystem` and `RenderDensityOverlaySystem` into a single `RenderDensitySystem` that:
- Does one grid iteration (within the culled AABB).
- For each tile with `agent_count > 0`:
  - If `show_data_layer`: draw the heat-map color overlay.
  - If `agent_count >= danger_threshold`: draw the pulsing flash overlay (always-on).

This reduces three full-grid passes to one partial-grid pass.

## Files to change

- `src/render_world.cpp` — All three systems, plus `BeginRenderSystem` for AABB calc
- `src/components.h` — Optional: add `VisibleRegion` struct if stored as a component
- `src/camera.h` — Already has `screen_to_grid()`, may need a batch version for corners

## Estimated impact

- **Performance:** ~60–80% fewer draw calls for the grid at typical zoom levels. Eliminating one full-grid pass entirely.
- **Complexity:** Net reduction of one system class. Grid iteration logic consolidated.

## Risks

- `screen_to_grid()` returns `std::optional` and can fail for off-screen points. Need to handle degenerate cases (fully zoomed out, rotated views).
- The density flash is drawn at `y=0.04f` and the overlay at `y=0.05f` — need to preserve correct Z-ordering when merged.
