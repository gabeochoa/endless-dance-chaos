# Phase 13: Timeline UI

**Demo 3C** | Prerequisite: Phase 12 (artist schedule works)

## Goal

Display upcoming artist schedule in a right sidebar, Google Calendar day-view style. "NOW" marker stays fixed, timeline scrolls beneath it.

**Done when:** Looking at the sidebar tells you exactly what's coming and when.

---

## Deliverables

### 1. Sidebar Layout

Right sidebar: 150px wide, full height minus top bar and build bar areas.

```
┌─────────────┐
│  TIMELINE   │
├─────────────┤
│             │
│  ▶ NOW      │ ← fixed at ~20% from top
│  ───────────│
│  Artist 472 │
│  10:00 ~100 │
│             │
│  Artist 831 │
│  12:00 ~150 │
│             │
│  (minimap   │
│   goes here │
│   later)    │
└─────────────┘
```

Use afterhours UI system for the sidebar container.

### 2. NOW Marker

- Horizontal line across the timeline
- Fixed at ~20% from the top of the timeline area
- Label: "▶ NOW" or "NOW" with a triangle indicator
- All artist blocks position relative to this marker

### 3. Artist Blocks

Each scheduled artist renders as a block:
- Height: 2px per game minute (30-min set = 60px, 1-hour set = 120px)
- Content: artist name, start time (HH:MM), expected crowd (~XXX)
- Font: Fredoka, name at 14px, numbers at 12px

Position calculation:
```cpp
// Minutes from NOW to artist start
float minutes_from_now = artist.start_time_minutes - clock.game_time_minutes;
// Convert to pixels (2px per game minute)
float y_offset = minutes_from_now * 2.0f;
// Offset from NOW marker position
float block_y = now_marker_y + y_offset;
```

### 4. Current Artist Highlight

When an artist is performing:
- Different background color (e.g., warm yellow `#FFD93D40`)
- "▶" indicator next to name

### 5. Auto-Scroll

Timeline auto-scrolls as time passes:
- NOW marker stays fixed
- Artist blocks move upward as time advances
- Past artists scroll off the top
- Future artists scroll into view from below

### 6. E2E Tests

**`tests/e2e/13_timeline.e2e`**:
```
# Test timeline display
reset_game
wait 10

expect_text "TIMELINE"
screenshot 13_timeline_initial

# Set time before artist
set_time 9 45
wait 30
screenshot 13_before_artist

# Set time during performance
set_time 10 30
wait 30
screenshot 13_during_performance

# Set time between artists
set_time 11 30
wait 30
screenshot 13_between_artists
```

---

## Existing Code to Leverage

- `ArtistSchedule` component from Phase 12
- `GameClock` from Phase 11
- Afterhours UI system
- Fredoka font

---

## Acceptance Criteria

- [ ] Timeline visible in right sidebar (150px wide)
- [ ] Artists listed in chronological order
- [ ] Current/performing artist highlighted
- [ ] NOW marker visible and fixed at ~20% from top
- [ ] Timeline scrolls as time passes (blocks move up)
- [ ] Block height = 2px per game minute
- [ ] Each block shows: name, time, crowd size

## Out of Scope

- Manual scroll / peek ahead (post-MVP)
- Hover tooltips
- Click to jump to stage
- Multiple stage timelines
