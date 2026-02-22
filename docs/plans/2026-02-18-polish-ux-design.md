# Polish Pass: Player Experience UX

**Date:** 2026-02-18
**Focus:** Onboarding clarity + mid-game readability
**Goal:** Make the game self-explanatory and visually scannable without relying on numbers or text parsing.

---

## 1. Contextual Hints System

Lightweight, non-intrusive hints that appear at the right moment. Each fires once per run, auto-dismisses after 6 seconds. Styled as a toast with a blue left-border accent to distinguish from gameplay toasts (green).

### New Component

```cpp
struct HintState : BaseComponent {
    std::bitset<8> shown;  // one bit per hint ID
    float game_elapsed = 0.f;
    int prev_death_count = 0;
    GameClock::Phase prev_phase = GameClock::Phase::Day;
    int prev_slots_per_type = 0;
};
```

### Triggers

| ID | Bit | Trigger | Message |
|----|-----|---------|---------|
| game_start | 0 | 5s elapsed, no paths placed | "Build paths from the GATE to the STAGE so attendees can find the music." |
| first_agents | 1 | First Agent entity exists | "Attendees are arriving! They follow paths to reach facilities." |
| first_need | 2 | Any AgentNeeds::needs_bathroom or needs_food flips true | "An attendee needs a break! Make sure paths connect to facilities." |
| first_death | 3 | death_count goes 0→1 | "An attendee was crushed! Spread crowds with more paths and facilities." |
| first_density | 4 | Any tile hits DENSITY_WARNING (50%) | "Crowd density rising! Press TAB for the density overlay." |
| night_falls | 5 | Phase transitions to Night | "Night phase: bigger crowds are coming. Get ready!" |
| first_exodus | 6 | Phase transitions to Exodus | "Exodus! Attendees are heading for the exits." |
| slot_unlocked | 7 | FacilitySlots::get_slots_per_type increases | "New facility slot unlocked! Check your build bar." |

### System

`HintCheckSystem` runs once per frame in the update loop. Checks each trigger condition against `HintState::shown` bits. When triggered, spawns a `ToastMessage` entity with `lifetime=6.0f` and a flag for hint styling.

---

## 2. Facility Fill Bars

Small horizontal bar rendered under each facility label in 3D-projected screen space.

### Visuals
- Width: ~30px, Height: ~4px
- Position: directly below the facility label text (WC, FOOD, MED)
- Fill ratio: `sum(agent_count across facility's 2x2 tiles) / FACILITY_MAX_AGENTS`
- Clamped 0.0–1.0
- Color: green (0–50%) → yellow (50–75%) → red (75%+)
- Only rendered when fill > 0 (no empty bars)
- Bar width stays constant regardless of capacity changes — purely proportional

### Implementation
- Extend `RenderFacilityLabelsSystem` to compute fill ratio per facility
- Add facility tile positions to `FacilityLabel` struct (or compute from anchor + 2x2 footprint)
- Draw a `DrawRectangle` bar below the projected label position

---

## 3. Speed Control Icons

Visible speed controls in the top bar, replacing the debug-only approach.

### Icons
- `||` — Pause
- `▶` — 1x (normal)
- `▶▶` — 2x (fast)
- `▶▶▶` — 4x (fastest)

### Layout
- Rendered in the top bar, after the phase name
- Four clickable regions, ~20x20px each, 4px gap
- Active speed is highlighted (bright white) vs inactive (dim gray)
- Clicking sets `GameClock::speed` directly
- SPACE keyboard shortcut continues to toggle pause

### Top Bar New Order
`time | phase | speed-icons | deaths | attendees | day | events | FPS`

---

## 4. Minimap Agent Dots

### Problem
Current minimap only shows tile layout. No way to see crowd distribution without panning.

### Solution
Split minimap into two layers:
1. **Tile layer** — cached in `g_minimap_texture`, only redrawn on `minimap_dirty` (existing)
2. **Agent layer** — drawn every frame on top of the tile layer

### Agent Dot Rendering
- Iterate all agents, project grid position to minimap coordinates
- Draw a 1–2px rectangle at the position
- Color matches desire pip: bathroom=teal, food=pink, stage=yellow, exit=blue, med=red
- Drawn after the cached tile texture, before the viewport rectangle

---

## 5. Density Warning Flash

Always-on visual warning for dangerous crowd density, without needing to toggle the overlay.

### Behavior
- Tiles at ≥75% density (`agent_count >= 30`) get a pulsing red-orange overlay
- Pulse speed increases as density approaches critical (90%+):
  - 75–89%: slow pulse (~1Hz)
  - 90%+: fast pulse (~3Hz)
- Rendered at y=0.04 (below density overlay at y=0.05, above tiles at y=0.01)
- Only iterates tiles with `agent_count > 0` for performance
- Independent of the TAB density overlay (always visible)

### Color
- 75–89%: `{255, 140, 0, alpha}` (orange, alpha pulses 0–80)
- 90%+: `{255, 40, 40, alpha}` (red, alpha pulses 40–140)

---

## 6. Death Location Markers

Visual breadcrumbs showing where crush deaths occurred.

### New Component

```cpp
struct DeathMarker : BaseComponent {
    vec2 position;
    float lifetime = 10.0f;
    float max_lifetime = 10.0f;
};
```

### Behavior
- Spawned at agent position when crush death occurs
- Rendered as a small red X on the ground plane (y=0.08)
- Two crossed lines, ~0.3 tile units wide
- Alpha fades out over the last 3 seconds
- Max 20 markers at once (oldest removed when exceeded)
- `DeathMarkerDecaySystem` decrements lifetime, destroys at 0

### Rendering
- Draw two crossed `DrawLine3D` calls per marker
- Color: `{255, 60, 60, alpha}` where alpha ramps from 255 to 0 in final 3 seconds

---

## 7. Hover Need Breakdown

Expanded tile hover info showing what agents on the tile want.

### Current
`(20, 25) Agents: 12`

### New
`(20, 25) Agents: 12 — 3 bathroom, 2 food, 7 watching`

### Implementation
- Only scan agents when hover position changes (cache last hover_x/hover_z)
- Iterate agents, check if their grid position matches hover tile
- Group by `Agent::want` (FacilityType enum)
- Only show categories with count ≥ 1
- Cache result until hover moves to a different tile

### Performance
- Scan is O(n) over all agents but only runs when hover changes (not every frame)
- At 500+ agents this is still fast enough for a one-shot scan

---

## 8. Bottleneck Auto-Toast

Proactive warning when a facility is overwhelmed.

### Behavior
- Every 10 seconds, check each facility's fill ratio
- If any facility exceeds 90% capacity for 5+ consecutive seconds, fire a toast
- Message: "Bathroom is overwhelmed — build another!" (or Food/Med Tent)
- Each facility TYPE only warns once per run (flag set in HintState or separate component)
- Only fires if the player has an available slot to build another (check FacilitySlots::can_place)

### New State
Add `bottleneck_warned` flags to `HintState` (or a separate `BottleneckState` component):
```cpp
bool bathroom_warned = false;
bool food_warned = false;
bool medtent_warned = false;
float bathroom_overload_timer = 0.f;
float food_overload_timer = 0.f;
float medtent_overload_timer = 0.f;
```

---

## Implementation Order

1. Contextual hints (new component + system, toast styling)
2. Speed control icons (top bar UI change)
3. Facility fill bars (extend existing label renderer)
4. Minimap agent dots (split minimap layers)
5. Density warning flash (new render system)
6. Death location markers (new component + render)
7. Hover need breakdown (extend existing hover)
8. Bottleneck auto-toast (new check system)

Items 1–3 are highest impact for onboarding. Items 4–6 are highest impact for mid-game readability. Items 7–8 are incremental improvements.
