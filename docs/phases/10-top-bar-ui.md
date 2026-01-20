# Phase 10: Top Bar UI

## Goal
Wire up the top bar placeholder to show actual game data: time, deaths, attendees.

## Prerequisites
- Phase 04 complete (UI skeleton)
- Phase 05 complete (time clock)
- Phase 02 complete (death tracking)

## Deliverables

### 1. Top Bar Data Display
Show in top bar (left to right):
- Time: `14:34` (24-hour format)
- Deaths: `Deaths: 3/10`
- Attendees: `Attendees: 847`

### 2. Layout
```
┌──────────────────────────────────────────────────────────────────┐
│  14:34  │  Deaths: 3/10  │  Attendees: 847                       │
└──────────────────────────────────────────────────────────────────┘
```

Use flex row with items spaced:
- Time on left
- Deaths in center-left
- Attendees center-right
- Right side empty (for future elements)

### 3. Font
Use Fredoka font:
- Size: 16px
- Load from `resources/fonts/Fredoka.ttf` (copy from wm_afterhours2)

### 4. Styling
- Background: Dark semi-transparent (#000000AA)
- Text: White
- Minimal — no fancy effects

## Existing Code to Use

- `GameClock::format_time()` for time display
- `GameState::death_count` and `MAX_DEATHS`
- Count agents with `Agent` component for attendees

## Afterhours to Use

- UI text components
- UI theme for colors
- Font loading via texture_manager or font_helper

## Implementation Steps

1. Copy Fredoka font to resources/fonts/
2. Load font at game init
3. Replace top bar placeholder with real components:
   - Text element for time
   - Text element for deaths
   - Text element for attendees
4. Create `UpdateTopBarSystem` (or update on render)
5. Query game state for values

## Acceptance Criteria

- [ ] Time displays and updates in real-time
- [ ] Deaths display and increment when agent dies
- [ ] Attendees count updates as agents enter/leave
- [ ] Text is readable (correct font size)
- [ ] Layout matches spec (items spaced across bar)

## Testing

### E2E Test Script: `test_top_bar.e2e`

```
# Test: Top bar UI
reset_game
wait 10

screenshot top_bar_initial

# Set specific time
set_time 14 34
wait 5
screenshot top_bar_time

# Spawn some agents
spawn_agents 0 0 50 stage
wait 30
screenshot top_bar_attendees

# Trigger some deaths
spawn_agents 0 0 40 stage
wait 300
screenshot top_bar_deaths
```

Run: `./output/dance.exe --test-mode --test-script="tests/e2e_scripts/test_top_bar.e2e"`

### Manual Testing

1. Verify time updates in real-time
2. Watch attendees count change as agents spawn
3. Trigger deaths and verify counter updates

## Out of Scope
- Death flash effect
- Phase indicator (Day/Night)
- Money display

