# Phase 12: Timeline UI

## Goal
Display upcoming artist schedule in sidebar, Google Calendar style.

## Prerequisites
- Phase 04 complete (UI skeleton with sidebar)
- Phase 09 complete (artist schedule system)

## Deliverables

### 1. Timeline Layout
In upper portion of sidebar (above minimap):
```
┌─────────────┐
│  TIMELINE   │
├─────────────┤
│  ▶ NOW      │ ← 20% from top
│  ───────────│
│  Artist 472 │ ← Current/upcoming
│  10:00 ~100 │
│             │
│  Artist 831 │
│  12:00 ~150 │
│             │
│  Artist 294 │
│  14:00 ~200 │
│             │
└─────────────┘
```

### 2. Block Sizing
- 2px per game minute
- 1-hour set = 120px tall
- Shows ~3-4 upcoming artists

### 3. NOW Marker
- Fixed at 20% from top
- Horizontal line across timeline
- Timeline scrolls beneath it

### 4. Artist Block Content
Each block shows:
- Artist name
- Start time (HH:MM)
- Expected crowd (~XXX)

### 5. Current Artist Highlight
- Different background color when performing
- "▶" indicator next to current

## Existing Code to Use

- `ArtistSchedule` component with today's artists
- `GameClock` for current time

## Afterhours to Use

- UI scrolling container (or manual scroll)
- UI text components

## Implementation Steps

1. Create timeline container in sidebar (above minimap)
2. Calculate visible time range based on current time
3. For each scheduled artist:
   - Calculate Y position based on start time
   - Create block with name, time, crowd
4. Draw NOW marker at fixed position
5. Offset all blocks so NOW aligns with current time

## Scroll Behavior
- Auto-follows current time (NOW marker stays fixed)
- Could add manual peek ahead (post-MVP)

## Acceptance Criteria

- [ ] Timeline visible in sidebar
- [ ] Artists listed in chronological order
- [ ] Current artist highlighted
- [ ] NOW marker visible
- [ ] Timeline scrolls as time passes
- [ ] Block height corresponds to set duration

## Out of Scope
- Hover tooltips
- Click to jump to stage
- Manual scrolling/peek ahead

