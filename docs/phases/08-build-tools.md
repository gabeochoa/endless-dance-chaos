# Phase 08: Build Tools

## Goal
Player can place paths, fences, gates, and facilities using controller-friendly input.

## Prerequisites
- Phase 06 complete (fence/gate components exist)
- Phase 07 complete (path tiles work)

## Deliverables

### 1. Build Tool Enum
Already exists in `components.h`, extend it:
```cpp
enum class BuildTool { 
    Path,      // Draw path rectangle
    Fence,     // Draw fence line
    Gate,      // Place 2×1 gate
    Stage,     // Place 4×4 stage
    Bathroom,  // Place 2×2 bathroom
    Food,      // Place 2×2 food
    Demolish   // Remove anything
};
```

### 2. Tool Cycling
- L bumper (or [ key): Previous tool
- R bumper (or ] key): Next tool
- Current tool shown in build bar

### 3. Cursor System
- Cursor follows D-pad/left stick or mouse
- Shows preview of what will be placed
- Green = valid placement
- Red = invalid (occupied, out of bounds)

### 4. Path Drawing
Rectangle drag mode:
1. A (or left click) at start corner
2. Move cursor to end corner (preview shows)
3. A again to confirm, B to cancel
4. Fills rectangle with path tiles

### 5. Facility Placement
Point placement:
1. Move cursor to desired location
2. A to place if valid
3. Shows facility size preview (2×2 or 4×4)

### 6. Demolish Mode
- Cursor shows X or demolish icon
- A on any placed object removes it
- Cannot demolish last gate

## Existing Code to Use

- `BuilderState` component with pending tiles
- `BuildTool` enum
- `make_*` entity makers
- Input mapping in `input_mapping.h`

## Afterhours to Use
- Input system for action mapping

## Implementation Steps

1. Add missing tools to enum
2. Create `HandleBuildInputSystem`:
   - Tool cycling with L/R bumper
   - Cursor movement
   - A/B for confirm/cancel
3. Create `RenderBuildPreviewSystem`:
   - Draw cursor highlight
   - Draw pending tiles preview
4. Create `CommitBuildSystem`:
   - On confirm, create actual entities
5. Add demolish logic

## Controller Mapping
| Input | Action |
|-------|--------|
| D-pad/Left Stick | Move cursor |
| A | Place/Confirm |
| B | Cancel/Delete pending |
| L Bumper | Previous tool |
| R Bumper | Next tool |

## Acceptance Criteria

- [ ] Can cycle through all 7 tools
- [ ] Cursor visible and moves with input
- [ ] Path drawing works (rectangle fill)
- [ ] Facilities can be placed
- [ ] Invalid placements show red
- [ ] B cancels pending placement
- [ ] Demolish removes placed objects
- [ ] Cannot delete last gate

## Testing

### E2E Test Script: `test_build_tools.e2e`

```
# Test: Building tools
reset_game
wait 10

# Cycle to bathroom tool
key RBRACKET
key RBRACKET
key RBRACKET
key RBRACKET
wait 5

screenshot tool_bathroom

# Place facility
click 640 360
wait 5

screenshot facility_placed

# Cycle to demolish
key RBRACKET
key RBRACKET
key RBRACKET
wait 5

screenshot tool_demolish
```

Run: `./output/dance.exe --test-mode --test-script="tests/e2e_scripts/test_build_tools.e2e"`

### Manual Testing

1. Press ] to cycle through tools
2. Verify cursor shows preview
3. Press A to place, verify placement
4. Try invalid placement, verify red highlight
5. Press B to cancel pending
6. Test demolish mode

## Out of Scope
- Build bar UI update (Phase 11)
- Cost system
- Facility limits based on progression

