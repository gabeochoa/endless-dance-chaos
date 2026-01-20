# Phase 11: Build Bar UI

## Goal
Display build tools in bottom bar with icons. Current tool highlighted and scaled up.

## Prerequisites
- Phase 04 complete (UI skeleton)
- Phase 08 complete (build tools functional)

## Deliverables

### 1. Tool Icons
Simple flat icons for each tool (can be placeholder colored squares initially):

| Tool | Icon Description | Color |
|------|------------------|-------|
| Path | Rectangle/grid | Brown |
| Fence | Vertical bars | Gray |
| Gate | Opening/door | Blue |
| Stage | Star/platform | Yellow |
| Bathroom | WC symbol | Cyan |
| Food | Fork/plate | Orange |
| Demolish | X mark | Red |

### 2. Layout
```
┌──────────────────────────────────────────────────────────────────┐
│  [Path] [Fence] [Gate] [Stage] [Bath] [Food] [Demolish]          │
└──────────────────────────────────────────────────────────────────┘
```

- Icons centered in bar
- Even spacing between icons
- Icon size: ~32×32px

### 3. Selection Indicator
Current tool gets:
- Highlight box around icon (2px border)
- Icon scales up 1.2x
- Different background color

### 4. Input Feedback
When L/R bumper pressed, selection should update immediately.

## Existing Code to Use

- `BuilderState::tool` — current tool
- `BuildTool` enum

## Afterhours to Use

- UI components for icons
- UI system for click handling (optional — can be keyboard only)

## Implementation Steps

1. Create icon assets (or use colored placeholder boxes)
2. Create flex row of icon elements
3. Track which icon matches current tool
4. Apply highlight/scale to selected
5. (Optional) Click on icon to select tool

## Icon Assets
For MVP, can just use colored squares with letters:
- P (Path), F (Fence), G (Gate), S (Stage), B (Bath), Fd (Food), X (Demolish)

Later: Create or find simple flat icons.

## Acceptance Criteria

- [ ] All 7 tools visible in build bar
- [ ] Current tool is visually highlighted
- [ ] Highlight updates when cycling with L/R
- [ ] Icons are readable and distinct
- [ ] Bar is positioned at bottom of screen

## Testing

### E2E Test Script: `test_build_bar.e2e`

```
# Test: Build bar UI
reset_game
wait 10

screenshot build_bar_default

# Cycle through tools
key RBRACKET
wait 5
screenshot build_bar_fence

key RBRACKET
wait 5
screenshot build_bar_gate

key RBRACKET
wait 5
screenshot build_bar_stage
```

Run: `./output/dance.exe --test-mode --test-script="tests/e2e_scripts/test_build_bar.e2e"`

### Manual Testing

1. Verify all 7 tool icons visible
2. Cycle with [ and ] keys
3. Verify current tool highlighted with border
4. Verify selected icon is scaled up

## Out of Scope
- Tool tooltips
- Tool cost display
- Locked tools (progression)

