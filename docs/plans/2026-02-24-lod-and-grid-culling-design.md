# LOD System & Grid Culling

**Date:** 2026-02-24
**Goal:** Support 100k agents at 60 FPS by rendering agents at different detail levels based on camera zoom, and culling off-screen tiles.

---

## Problem

- `RenderAgentsSystem` draws 2 cubes (body + desire pip) per agent. At 100k agents = 200k draw calls.
- `RenderGridSystem` draws all 2,704 tiles every frame regardless of camera position.
- `RenderDensityFlashSystem` and `RenderDensityOverlaySystem` each iterate all 2,704 tiles independently.

## Design

### Visible Region (Grid Culling)

Computed once per frame in `BeginRenderSystem`:

1. Project 4 screen corners to grid coordinates via `screen_to_grid()`
2. Compute AABB: `min_x, max_x, min_z, max_z`
3. Add 2-tile margin, clamp to `[0, MAP_SIZE)`
4. Store in `VisibleRegion` singleton

All tile-based rendering loops use this AABB instead of `0..MAP_SIZE`.

### LOD Tiers

Based on camera `fovy` (orthographic zoom level, range 5–50):

| Tier | fovy | Agent Rendering | Cost |
|------|------|-----------------|------|
| **Close** | < 15 | Individual cubes, viewport-culled | O(agents) iteration, O(visible agents) draw |
| **Medium** | 15–30 | Per-tile scattered dots from `agent_count` | O(visible tiles) |
| **Far** | > 30 | Animated density overlay from `agent_count` | O(visible tiles) |

At medium and far zoom, agent entities are never iterated for rendering. Cost is purely tile-driven.

### Close LOD (fovy < 15)

- Current `RenderAgentsSystem` rendering (body cube + desire pip)
- Changed from `System<Agent, Transform>` to `System<>` with manual query
- Early return if LOD != Close
- Viewport culling: skip agents whose grid position falls outside `VisibleRegion`
- At close zoom, ~100–500 agents visible even with 100k total

### Medium LOD (fovy 15–30)

Per-tile dot rendering:

- For each visible tile with `agent_count > 0`, draw `min(agent_count, 6)` small planes
- Dot positions: deterministic scatter within tile via hash (same `hash_scatter` as agent rendering)
- Time-based jitter: slow sinusoidal offset for "alive" feeling
- Dot colors: cycle through agent body palette
- Dot size: 0.1 tile units, drawn as planes at y=0.10
- Max draw calls: ~6 * visible_occupied_tiles (typically 3,000–6,000)

### Far LOD (fovy > 30)

Desire-colored blobs that merge across adjacent occupied tiles:

- **Desire-based coloring**: Each blob's color reflects the agents' current desires on that tile
  - Stage → gold, Bathroom → teal, Food → pink, Exit → blue, MedTent → red
  - Per-tile color = weighted average of desire colors based on `Tile::desire_counts[]`
  - Colors shift in real-time as agents move and change desires
- **Three-pass rendering** per occupied tile:
  1. **Broad glow** (r ≈ 1.3–2.0 tiles): neighbor-blended color, low alpha. Large enough to overlap adjacent tiles, creating seamless merged blobs with smooth color transitions at edges
  2. **Medium core** (r ≈ 0.6–1.1 tiles): tile's own desire color, medium alpha
  3. **Dense peak** (r ≈ 0.3–0.6 tiles): brightened tile color, high alpha (only for density > 0.15)
- **Neighbor blending**: Pass 1 averages the tile's color with its 8 neighbors (cardinal=0.5 weight, diagonal=0.25 weight), creating smooth color lerping where different-desire blobs touch
- Per-tile phase offset + time-based radius pulsing for organic animation
- Rendered as flat cylinders (height=0.001, 16 segments) for smooth round shapes
- Three draw calls per occupied tile (typically 3,000–6,000 draw calls)

### Density Overlay Merge

Combine `RenderDensityFlashSystem` and `RenderDensityOverlaySystem` into a single `RenderDensitySystem`:

- One iteration over visible tiles (not full grid)
- Handles both the TAB-toggle heat map and the always-on danger flash
- Runs at all LOD levels (density info is always relevant)

### New Component

```cpp
enum class LODLevel { Close, Medium, Far };

struct VisibleRegion : afterhours::BaseComponent {
    int min_x = 0, max_x = MAP_SIZE - 1;
    int min_z = 0, max_z = MAP_SIZE - 1;
    LODLevel lod = LODLevel::Close;
};
```

## Files Changed

- `src/components.h` — Add `VisibleRegion`, `LODLevel`
- `src/entity_makers.cpp` — Register `VisibleRegion` singleton
- `src/render_world.cpp` — All rendering changes (culling, LOD systems, density merge)

## Future Work

- Minimap agent dots: at 100k agents, iterating all for 2px dots is expensive. Could sample every Nth agent or use tile-based dots on minimap too.
- GPU instancing: if medium LOD dots become a bottleneck, batch into a single instanced draw call.
- Smooth LOD transitions: blend between tiers instead of hard cutoff (low priority).
