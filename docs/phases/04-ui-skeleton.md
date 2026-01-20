# Phase 04: UI Skeleton

## Goal
Set up the UI layout with placeholder boxes. No functionality yet — just visual structure to confirm layout before building real UI.

## Prerequisites
- Phase 03 complete (game loop works end-to-end)

## Deliverables

### 1. UI Layout Structure
```
┌─────────────────────────────────────────────────────────────────┐
│  [TOP BAR - 40px height]                                        │
│  "Time: --:-- | Deaths: 0/10 | Attendees: 0"                   │
├───────────────────────────────────────────────┬─────────────────┤
│                                               │ [SIDEBAR 150px] │
│                                               │                 │
│                                               │ "Timeline"      │
│              [GAME VIEW]                      │ (placeholder)   │
│              (no UI here)                     │                 │
│                                               │ ─────────────── │
│                                               │ "Minimap"       │
│                                               │ (placeholder)   │
├───────────────────────────────────────────────┴─────────────────┤
│  [BUILD BAR - 50px height]                                      │
│  "Path | Fence | Gate | Stage | Bath | Food | Demolish"        │
└─────────────────────────────────────────────────────────────────┘
```

### 2. Placeholder Boxes
Each UI region should be:
- Visible colored box with border
- Label text saying what will go there
- Correct size/position per spec

### 3. No Game View Overlap
The game camera viewport should NOT render behind UI. Adjust game rendering area.

## Existing Code to Use

- `game.h` has `DEFAULT_SCREEN_WIDTH` (1280) and `DEFAULT_SCREEN_HEIGHT` (720)

## Afterhours to Use

- **UI system**: `afterhours/src/plugins/ui.h`
- **Autolayout**: Flexbox-style layout
- **Theme**: For consistent colors

See `vendor/afterhours/example/ui_layout/` for example usage.

## Implementation Steps

1. Include afterhours UI headers
2. Create UI root entity with full screen size
3. Add top bar (fixed height 40px, full width)
4. Add main row (flex: column with row inside)
   - Game area (flex-grow)
   - Sidebar (fixed width 150px)
5. Add bottom build bar (fixed height 50px)
6. Each placeholder: colored background + centered text

## Layout Constants (add to `game.h`)
```cpp
constexpr int UI_TOP_BAR_HEIGHT = 40;
constexpr int UI_BUILD_BAR_HEIGHT = 50;
constexpr int UI_SIDEBAR_WIDTH = 150;
constexpr int UI_MINIMAP_SIZE = 150;
```

## Acceptance Criteria

- [ ] Top bar visible at top of screen (full width, 40px tall)
- [ ] Sidebar visible on right (150px wide)
- [ ] Build bar visible at bottom (full width, 50px tall)
- [ ] Game view fills remaining space
- [ ] Placeholder text visible in each region
- [ ] UI does not overlap game view

## Testing

### E2E Test Script: `test_ui_skeleton.e2e`

```
# Test: UI skeleton layout
reset_game
wait 10

screenshot ui_skeleton_layout
```

Run: `./output/dance.exe --test-mode --test-script="tests/e2e_scripts/test_ui_skeleton.e2e"`

### Manual Testing

1. Run game
2. Verify UI regions are visible:
   - Top bar at top (40px height)
   - Sidebar on right (150px width)
   - Build bar at bottom (50px height)
3. Verify placeholder text shows in each region
4. Compare screenshot to spec diagram

## Out of Scope
- Actual data in UI (Phase 10-13)
- Interactivity
- Final styling/colors
- Font loading (use default)

