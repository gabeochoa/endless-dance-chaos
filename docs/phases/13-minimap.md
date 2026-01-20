# Phase 13: Minimap

## Goal
Show map overview in sidebar with paths, buildings, and camera viewport rectangle.

## Prerequisites
- Phase 04 complete (UI skeleton with sidebar)
- Phase 06 complete (fence/gate exist)

## Deliverables

### 1. Minimap Layout
Bottom of sidebar, 150×150px:
```
┌─────────────┐
│             │
│  [MINIMAP]  │
│             │
│  ┌───┐      │  ← Camera viewport
│  │   │      │
│  └───┘      │
│             │
└─────────────┘
```

### 2. What to Render
- Paths: Light color
- Fence: Dark outline
- Gates: Gap in fence
- Stage: Yellow square
- Bathroom: Cyan square
- Food: Orange square
- Camera viewport: White rectangle outline

### 3. NO Agents
Density overlay is separate (Phase 14). Minimap is static layout only.

### 4. Click to Jump
- Click on minimap = instant camera jump to that position
- Drag on minimap = continuous panning

### 5. Scale Calculation
```cpp
// Map is 50×50 tiles, minimap is 150×150px
float minimap_scale = 150.0f / 50.0f;  // 3px per tile
```

## Existing Code to Use

- `PathTile` query for paths
- `Fence` query for fences
- `Facility` query for buildings
- `ProvidesCamera` for viewport position/size

## Afterhours to Use

- UI for container/click handling
- Or render directly with raylib in render pass

## Implementation Steps

1. Create minimap render target or draw area
2. Query all PathTiles, draw scaled rectangles
3. Query all Fences, draw scaled lines
4. Query all Facilities, draw colored squares
5. Calculate camera viewport rectangle
6. Draw viewport outline
7. Handle mouse/touch input on minimap

## Viewport Rectangle Calculation
```cpp
// Get camera's view bounds in world coordinates
float view_width = camera.distance * 2;  // Ortho camera
float view_height = view_width * (720.0f / 1280.0f);  // Aspect ratio

// Convert to minimap coordinates
float minimap_x = (camera.target.x + 25) * minimap_scale;
float minimap_y = (camera.target.z + 25) * minimap_scale;
float minimap_w = view_width * minimap_scale;
float minimap_h = view_height * minimap_scale;
```

## Acceptance Criteria

- [ ] Minimap visible in sidebar (150×150px)
- [ ] Paths shown in minimap
- [ ] Buildings shown as colored squares
- [ ] Fence perimeter visible
- [ ] Camera viewport rectangle visible
- [ ] Click on minimap jumps camera
- [ ] Viewport rectangle updates as camera moves

## Testing

### E2E Test Script: `test_minimap.e2e`

```
# Test: Minimap
reset_game
wait 10

screenshot minimap_initial

# Place some facilities
place_facility bathroom 5 5
place_facility food 5 -5
wait 10

screenshot minimap_with_facilities

# Move camera and verify viewport moves
key W
key W
key W
wait 10

screenshot minimap_camera_moved
```

Run: `./output/dance.exe --test-mode --test-script="tests/e2e_scripts/test_minimap.e2e"`

### Manual Testing

1. Verify minimap shows paths and fence
2. Verify buildings appear as colored squares
3. Move camera, verify viewport rectangle updates
4. Click on minimap, verify camera jumps

## Out of Scope
- Agent density display
- Zooming minimap
- Minimap rotation

