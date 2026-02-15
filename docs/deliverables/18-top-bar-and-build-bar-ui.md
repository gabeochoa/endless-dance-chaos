# Phase 18: Top Bar & Build Bar UI

**Demo 4C** | Prerequisite: Phase 17 (progression system)

## Goal

Wire up proper UI: top bar shows time, deaths, attendees. Bottom build bar shows tool icons with selection indicator.

**Done when:** All critical info visible at a glance, tools easy to select.

---

## Deliverables

### 1. UI Layout (Afterhours)

Use afterhours UI system with autolayout:

```
┌──────────────────────────────────────────────────────────────────┐
│  14:34  │  Deaths: 3/10  │  Attendees: 847                       │ ← 40px
├───────────────────────────────────────────────┬──────────────────┤
│                                               │ (sidebar 150px)  │
│              GAME VIEW                        │ (timeline above) │
│                                               │ (minimap below)  │
├───────────────────────────────────────────────┴──────────────────┤
│  [P] [F] [G] [S] [B] [Fd] [X]                                    │ ← 50px
└──────────────────────────────────────────────────────────────────┘
```

### 2. Top Bar (40px height)

Content, left to right:
- Time: `"14:34"` (24-hour format from GameClock)
- Phase indicator: `"Day"` / `"Night"` / `"Exodus"` / `"Dead Hours"`
- Deaths: `"Deaths: 3/10"` (red text when >= 7)
- Attendees: `"Attendees: 847"` (current agent count)

Style:
- Background: `#000000AA` (dark semi-transparent)
- Text: white, Fredoka 16px
- Flex row with items spaced evenly

### 3. Build Bar (50px height)

Content: 7 tool icons in a centered flex row.

| Tool | Label | Color |
|------|-------|-------|
| Path | P | `#B8A88A` (brown) |
| Fence | F | `#888888` (gray) |
| Gate | G | `#4488AA` (blue) |
| Stage | S | `#FFD93D` (yellow) |
| Bathroom | B | `#7ECFC0` (cyan) |
| Food | Fd | `#F4A4A4` (coral) |
| Demolish | X | `#FF4444` (red) |

Each icon:
- 32x32 colored square with letter label (Fredoka 12px, centered)
- 4px gap between icons

### 4. Selection Indicator

Current tool:
- 2px white border around icon
- Icon scales up 1.2x
- Slightly brighter background

Updates immediately when tool changes ([ / ] keys or 1-7).

### 5. Toast Area

Toast messages (from Phase 17) appear in the top bar area:
- Right side of top bar
- Semi-transparent background
- Fades after 2-3 seconds

### 6. Game View Clipping

The game camera viewport should NOT render behind UI:
- Adjust render area to exclude top bar (40px), sidebar (150px), and build bar (50px)
- Use raylib `BeginScissorMode()` or set viewport bounds

### 7. Replace Debug HUD

Remove the temporary debug HUD from Phase 05. All info now in proper UI.

### 8. E2E Tests

**`tests/e2e/18_top_bar.e2e`**:
```
# Test top bar content
reset_game
wait 10

set_time 14 34
wait 5
expect_text "14:34"
expect_text "Deaths: 0/10"
expect_text "Attendees:"
screenshot 18_top_bar

# Spawn agents
spawn_agents 25 25 50 stage
wait 30
screenshot 18_top_bar_with_agents
```

**`tests/e2e/18_build_bar.e2e`**:
```
# Test build bar
reset_game
wait 10

screenshot 18_build_bar_default

# Cycle tools
key RBRACKET
wait 5
screenshot 18_build_bar_fence

key RBRACKET
wait 5
screenshot 18_build_bar_gate

# Direct select
key 5
wait 5
screenshot 18_build_bar_bathroom
```

---

## Existing Code to Leverage

- Afterhours UI system (`vendor/afterhours/src/plugins/ui.h`)
- `GameClock::format_time()` from Phase 11
- `GameState::death_count` from Phase 07
- `BuilderState::tool` from Phase 16
- Fredoka font from Phase 01

---

## Acceptance Criteria

- [ ] Top bar displays: time, phase name, deaths, attendees
- [ ] All values update in real-time
- [ ] Build bar shows 7 tool icons
- [ ] Current tool has highlight border + scale-up
- [ ] Highlight updates when cycling with [ / ]
- [ ] Game view does not render behind UI elements
- [ ] Toast messages display correctly in top bar area
- [ ] Font: Fredoka throughout

## Out of Scope

- Tool tooltips
- Slot count display per tool
- Locked tool indicators
