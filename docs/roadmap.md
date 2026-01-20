# Endless Dance Chaos - Development Roadmap

## Vision

A **Mini Metro-style minimalist management game** about running a music festival.

- **Success**: Survive as long as possible, grow to 100k attendees
- **Failure**: 10 crowd crush deaths = Game Over
- **Core tension**: Attract more people (growth) vs. keep them safe (survival)

**One-sentence pitch**: "Tower defense, but the towers are bathrooms and the creeps are festival-goers."

---

## Controller-Friendly Design Constraint

**The game must be playable with ~10 inputs:**

| Button | Action |
|--------|--------|
| D-pad/Left Stick | Move cursor |
| A | Place/Confirm |
| B | Delete/Cancel |
| L Bumper | Cycle tool left |
| R Bumper | Cycle tool right |
| Y | Toggle sidebar |
| Start | Pause |
| L/R Triggers | Rotate camera |
| Right Stick | Move camera |

**Rule**: If a feature can't fit this input scheme, it's too complex.

---

## Technical Specs

### Grid System
- **Tile size**: 32×32 pixels (at 1x zoom, 720p)
- **Map size**: 50×50 tiles (1600×1600 pixels) — fixed for MVP
- **Real-world scale**: 1 tile = 2m × 2m = 4 m²
- **Target**: 120 FPS, minimum 720p resolution
- **Resolution scaling**: Letterboxing (fixed game window, black bars for excess space)
- **Performance**: Must maintain 60 FPS at 100k agents (aggressive LOD required)
  - Frame budget: ~8ms logic, ~8ms render (16ms total)
  - Agents updated in batches across frames (imperceptible to user)

### Density System
- **Max capacity**: 40 agents per tile (based on 5× fire code density)
- **Thresholds**:
  - Normal: 0-20 agents (<50%)
  - Warning: 20-30 agents (50-75%)
  - Dangerous: 30-36 agents (75-90%)
  - Critical: 36-40+ agents (>90% — crush damage begins)

---

## Visual Style: RollerCoaster Tycoon 2 Style

### Isometric View
- **Projection**: Standard 2:1 isometric (like RCT2)
- **Camera**: RCT-style isometric with 4 rotations (90° increments, instant snap)
- **Orientation indicator**: Show "N" compass indicator on screen
- **Zoom**: 3 levels
  - Close: ~25 tiles visible (see agent detail)
  - Medium: ~40 tiles visible (natural 720p view)
  - Far: ~60 tiles visible (entire map + margin)
- **Camera speed**: Scales with zoom level (~10 tiles/sec at medium)
- **Camera bounds**: 10 tiles padding past map edge allowed

### Color Palette

**Day (10am-6pm) — Miami Pastel:**
| Element | Hex |
|---------|-----|
| Ground | #F5E6D3 (warm sand) |
| Grass | #98D4A8 (soft mint) |
| Path | #E8DDD4 (light concrete) |
| Accents | #7ECFC0 (teal), #F4A4A4 (coral) |
| Stage | #FFD93D (sunny yellow) |
| UI | #FFF8F0 (warm off-white) |

**Night (6pm-3am) — EDC Neon:**
| Element | Hex |
|---------|-----|
| Ground | #1A1A2E (deep navy) |
| Grass | #2D4A3E (dark forest) |
| Path | #2A2A3A (dark slate) |
| Accents | #00F5FF (cyan), #FF00AA (magenta) |
| Stage | #FFE600 (bright yellow lights) |
| UI | #16162A (dark purple) |

**Transition**: Gradual fade over 1 hour game time

### Agent Visuals
- **Sprite size**: 8×8 pixels (tiny dots that pile up into crowds)
- **Animation**: 2 frames (simple toggle)
- **Directions**: None — same sprite always (MVP)
- **Colors**: Natural/human appearance — skin tones and realistic clothing colors (not arbitrary bright colors)
- **Death effect**: Particle burst (5-8 square particles, 2-4px, 8-12px radius, 0.3-0.5 sec duration)
- **Mass death**: Multiple simultaneous deaths merge into one bigger explosion

### LOD System (100k agents)
| Zoom Level | Visual |
|------------|--------|
| Close | 8×8 sprites with 2-frame animation |
| Medium | Simplified sprites, batch rendering |
| Far | Heat map gradient overlay (green→yellow→red) |

### Danger Visualization
- **Overlay**: Gradient heat map (per-tile data, smoothly interpolated)
  - 0% density: Transparent (no overlay)
  - 50% density: Yellow (#FFFF00)
  - 75% density: Orange (#FFA500)
  - 90% density: Red (#FF0000)
  - 100% density: Black (#000000)
- **Death visual**: Agent pops → particle burst in agent's color
- **Screen shake**: None for MVP

---

## Time System

### Continuous Time (No Discrete Days)

The clock always ticks. No hard day boundaries - just phases:

```
10am          6pm           Midnight      3am           10am
 │─────────────│──────────────│─────────────│─────────────│
 │  Day Phase  │  Night Phase │   Exodus    │ Dead Hours  │
 │  Artists    │  Headliners  │  Clear out  │  Rebuild    │
 │  perform    │  big crowds  │  everyone   │  empty map  │
```

### Time Phases

| Phase | Game Time | Real Time | What Happens |
|-------|-----------|-----------|--------------|
| **Day** | 10am - 6pm | 3 min | Artists perform, moderate crowds |
| **Night** | 6pm - Midnight | 3 min | Headliners, largest crowds, peak danger |
| **Exodus** | Midnight - 3am | 3 min | ALL attendees must exit. Deaths still count! |
| **Dead Hours** | 3am - 10am | 3 min | Empty festival. Build/reorganize in peace. |
| **Total cycle** | 24 hours | **12 min** | |

**Time display**: 24-hour format (14:34)

### Time Controls
- **Spacebar**: Full pause - freezes time but allows building/camera movement
- **Escape**: Pause menu
- **Speed**: 1x only for MVP (2x/4x later)

### Exodus Rules
- Agents who can't exit in time **stay for next day** (carryover punishment)
- Dead Hours (3am-10am): Standard gameplay, just no agents — full building access

---

## Game Loop (Tower Defense Style)

```
See Lineup → Prepare Layout → Artist Performs → Crowd Surges → Survive → Next Artist
                                                     ↓
                                              Density Check
                                                     ↓
                               Too Crowded? → Crush → Deaths → 10 = Game Over
```

### Artist Schedule System

**MVP: One artist at a time, one stage.**

1. **Artist names**: "Artist XXX" (random 3-digit number) for MVP — fancy names post-MVP
2. **Performance duration**: 1-hour sets in 2-hour slots (~4 artists per day phase)
3. **Continuous scaling**: Artist crowd sizes grow with festival size (no hard tiers)
   - Early (0-500 attendees): 50-200 crowd, 30 min sets
   - Mid (500-2k): 200-800 crowd, 30-45 min sets
   - Late (2k+): 500-2000+ crowd, 45-60 min sets
4. **Spawn rate**: Tied to upcoming artist's crowd size — bigger artists = faster spawning
5. Timeline shows what's coming so player can prepare
6. **Stage effect**: Color tint overlay when artist starts performing (simple for MVP)

### Spawning & Flow
- **Map starts with**: Perimeter fence + 1 gate + 1 stage pre-placed
- **Agents spawn** on grass outside fence, enter through gates
- **Entry animation**: Either fade in near gate OR appear in "bus stop" area then walk to gate (intern's choice)
- **Arrival pattern**: Continuous stream, ramps up ~15 min before each performance
- On spawn, agent has:
  - Target artist they want to see
  - Optional secondary needs (food, bathroom)
  - Patience timer (60-120 sec — starts when pathing fails)
- **Goal priority**: Urgency-based (bathroom > food > stage), but agents avoid visibly crowded areas
- **Facility search**: Try next 2-3 closest of same type if first is full
- **Stage watching**: Random duration per agent (30-120 sec), then leave or get needs
- **Cannot reach goal**: Wander randomly
- **Patience expired**: Walk to nearest exit (TBD — may tune during playtesting)

---

## Core Systems

### 1. The Crush Mechanic (Primary Danger)

| Density Level | Agents/Tile | Visual | Effect |
|--------------|-------------|--------|--------|
| Normal | 0-20 (<50%) | Green | Safe |
| Warning | 20-30 (50-75%) | Yellow | Caution |
| Dangerous | 30-36 (75-90%) | Orange | Movement slows |
| Critical | 36-40+ (>90%) | Red gradient | Crush damage begins |

**Crush Damage**:
- Agent health: **1 HP**
- Damage rate: **0.2/sec** (flat, no scaling)
- Time to death: **~5 seconds** in critical zone
- **10 deaths = Game Over**

**Death Visual**: Agent pops → particle burst in agent's color (quick, punchy)

### 2. Agent Movement (Boids-Inspired)

**Base speed**: 0.5 tiles/sec (1 m/s real-world) — slow festival shuffle
**Separation radius**: 0.25 tiles (8px) — tight packing allowed

| Force | Description |
|-------|-------------|
| **Goal Pull** | Move toward current target (stage, exit, facility) |
| **Pheromone Following** | Follow trails left by other agents |
| **Separation** | Push away when within 0.25 tiles |
| **Path Preference** | Prefer paths over grass |

**Movement Speeds**:
- **Paths**: Full speed (0.5 tiles/sec)
- **Grass**: Half speed (0.25 tiles/sec)
- **Fence**: Blocks movement completely
- **Dangerous zone (75-90%)**: Linear slowdown — speed decreases proportionally with density

### 3. Pheromone Trail Pathfinding (BGP-style)

Agents don't have global knowledge. Emergent pathfinding:

1. Agents **leaving** a stage/facility mark tiles with direction hints
2. More agents leaving = stronger pheromone trail
3. New agents follow strongest trails toward destinations
4. Trails decay over time if not reinforced
   - **Trail lifespan**: 60 seconds
   - **Fresh strength**: 10 (1-10 scale)
   - **Decay rate**: ~0.17/sec (loses 1 point every 6 sec)
5. Recalculated async over several frames (avoid frame drops)

**Creates emergent behavior**:
- Popular routes get reinforced naturally
- Bottlenecks form organically
- Early-game pathfinding is messy, improves as crowd grows

**No coverage fallback**: Agents beeline directly toward goal (may get stuck)

**"Festival Pathing" overlay** (debug/player tool):
- Toggle to show arrows on each tile
- Filter by facility type: Stage, Bathroom, Food, Exit
- Helps player understand crowd flow

### 4. Facilities

| Facility | Size | Real-world | Effect |
|----------|------|------------|--------|
| **Stage** | 4×4 tiles | 8m × 8m | Attracts agents, lights up during performance |
| **Bathroom** | 2×2 tiles | 4m × 4m | 1 sec service, absorbs agents |
| **Food** | 2×2 tiles | 4m × 4m | 1 sec service, absorbs agents |
| **Gate** | 2×1 tiles | 4m × 2m | Entry/exit point in fence |
| **Fence** | 1×1 tile | 2m segment | Blocks all movement |
| **Path** | 1 tile wide | 2m | Fast movement, can merge into plazas |

**Stage "watching" zone**: Semi-circle, 8-10 tile radius in front of stage
- "Front" = the side touching the path (stage faces toward adjacent path tiles)
- Agents in zone stop moving and watch

**Queue capacity**: Same as tile density (40 agents) — agents crowd around facility

**Agent behavior at full queue**:
- Urgent need (bathroom): Try another facility of same type
- Non-urgent: Give up and wander

**Queue Management** (Post-MVP):
- Queue lane upgrade: faster service but lower capacity

### 5. Festival Progression

Festival grows based on **max attendees ever in park**:

**Simple formula: +1 of each facility type per 100 attendees**

| Attendees | Stages | Bathrooms | Food | Gates |
|-----------|--------|-----------|------|-------|
| Start | 1 | 1 | 1 | 1 |
| 100 | 2 | 2 | 2 | 2 |
| 200 | 3 | 3 | 3 | 3 |
| etc. | +1 | +1 | +1 | +1 |

More capacity = bigger festivals = bigger artists = more danger.

---

## Building System

### Build Tools (Cycle with L/R)
1. **Path** - Draw paths (click start → click end → confirm)
2. **Fence** - Draw fence lines
3. **Gate** - Place entry/exit points in fence
4. **Stage** - Place stage
5. **Bathroom** - Place bathroom facility
6. **Food** - Place food facility
7. **Demolish** - Remove anything

### Building Rules
- **Paths and Fences**: Free, unlimited
- **Facilities**: Limited by progression slots (+1 per 100 attendees)
- **Timing**: Build any time (paused or live) — react in real-time
- **Cost**: None for MVP
- **Placement**: Blocked on occupied tiles (red highlight only, no text feedback)
- **Cursor**: Highlighted tile with semi-transparent fill
- **Movement**: Facilities can be picked up and moved freely
- **Path drawing**: A to start → move cursor → A to confirm (B to cancel) → preview shown during drag

### Demolition
- Instant removal
- Cannot delete last gate (prevents soft-lock)
- No undo (can just rebuild - it's free)

---

## UI Layout

```
┌─────────────────────────────────────────────────────────────────┐
│  TIME: 2:34 PM | DAY 3 | DEATHS: 3/10 | ATTENDEES: 847 | $$$   │ ← Top Bar
├───────────────────────────────────────────────────┬─────────────┤
│                                                   │ TIMELINE    │
│                                                   │             │
│                                                   │ ▶ DJ Chill  │
│              MAIN GAME VIEW                       │   2pm ~200  │
│              (Isometric)                          │             │
│                                                   │   Rock Band │
│                                                   │   4pm ~500  │
│                                                   │             │
│                                                   │ ★ LEGEND ★  │
│                                                   │   11pm ~2k  │
│                                                   ├─────────────┤
│                                                   │ [MINIMAP]   │
│                                                   │ (density    │
│                                                   │  overlay)   │
├───────────────────────────────────────────────────┴─────────────┤
│  [Path] [Fence] [Gate] [Stage] [Bath] [Food] [Demolish]         │ ← Build Bar
└─────────────────────────────────────────────────────────────────┘
```

### Font & Styling
- **Font**: Fredoka (friendly rounded sans-serif)
- **Font sizes**: Top bar 16px, timeline artist names 14px, timeline numbers 12px, build bar 12px

### Top Bar
- Current time (24-hour format: 14:34)
- Death counter (Deaths: 3/10) — increment only, no flash (MVP)
- Attendees currently in park

### Right Sidebar (150px width)
- **Timeline**: Google Calendar day view style
  - "NOW" marker fixed at ~20% from top
  - Timeline scrolls upward as time passes
  - Events are blocks sized by duration: **2px per game minute** (30-min set = 60px)
  - Per artist: name, duration, expected crowd size (number)
  - Civ-style scroll: auto-follows current, manual peek ahead
  - No hover state for MVP
- **Minimap** (150×150px, bottom of sidebar)
  - Shows map layout only (paths, buildings, fence, gates)
  - NO agent density (use TAB overlay for that)
  - Shows camera viewport rectangle
  - Click = instant camera jump, drag = continuous panning

### Build Bar (Bottom)
- Simple flat icons
- Current tool: highlight box + icon scales up
- Cycle with L/R bumpers

---

## Game States

### Main Menu
Simple: New Game, Options, Quit

### Playing
Normal gameplay with pause (Spacebar) and pause menu (Escape)

**Pause Menu**: Resume, Restart, Options, Main Menu, Quit

### Game Over
Triggered at 10 deaths.

**Transition**: Brief pause (2-3 sec) showing the fatal death, then Game Over screen

**Game Over Screen**:
- "FESTIVAL SHUT DOWN"
- Stats: Days survived, max attendees, total served
- Heatmap of death locations (post-MVP)
- "Try Again" button
- No high score tracking for MVP (add post-MVP)

### Victory
**Endless mode** - no formal victory. Goal is survival and growth.

---

## The Emotional Loop

**Core Feel**: Spinning plates until it all comes crashing down. The tension of juggling multiple growing problems, knowing eventually something will slip — and that glorious/tragic moment when it finally does.

### Success Feeling
"Damn, I just survived a 2000-person headliner rush with zero deaths. My path layout worked perfectly. Like beating a boss in tower defense - held my breath the whole time."

### Failure Feeling
"I was SO close! Juggling all those balls, spinning all those plates... but I know exactly what went wrong. That bottleneck near the stage killed me. Next time I'll widen that path BEFORE the headliner."

---

## Visual References

- **Breaking the Tower (Notch)** — **STUDY THIS CLOSELY** for creative tower defense feel and emergent chaos
- **Mini Motorways** (minimalist flow, emergent complexity)
- **RollerCoaster Tycoon 2** (isometric view, nostalgic charm)
- **Train Valley 1 & 2** (path drawing, timing challenge)
- **Package Inc** (logistics management)
- **Freeways** (road drawing)

---

## MVP Implementation Priorities

| Priority | Feature | Status |
|----------|---------|--------|
| 1 | Crush death mechanic (density → damage → deaths) | Pending |
| 2 | Time & schedule system (clock, phases, artists) | Pending |
| 3 | Fence + Gate system (entry/exit control) | Pending |
| 4 | Timeline UI (upcoming artists + crowd sizes) | Pending |
| 5 | Path vs grass movement (paths fast, grass slow) | Pending |
| 6 | Basic facility flow (bathroom, food, stage) | Partial |
| 7 | Game Over screen + stats | Pending |
| 8 | Post-game stats display | Pending |

### Starting State
Game begins with:
- Pre-built **perimeter fence** around 50×50 play area
- **One gate** (can't delete last gate)
- **One stage** pre-placed
- Player builds paths and additional facilities from there
- **No tutorial** — learn by doing

### OUT OF MVP SCOPE
- Sound effects / Music (post-MVP: minimalist ambient style like Mini Metro)
- Tutorial (post-MVP: optional tutorial level)
- Save / Load
- Speed controls (2x, 4x)
- Multiple stages
- Merch / Water facilities
- Weather system
- Staff/security units
- Heatmap replay
- Complex management menus
- Screen shake / juice effects (except stage lights up)
- High score tracking (add post-MVP)
- Colorblind accessibility patterns (add post-MVP)

---

## Implementation Checklist

### Phase 1: Core Crush Mechanic
- [ ] Track agent density per tile (max 40 agents = 100%)
- [ ] Density thresholds: 50%/75%/90% for warning/dangerous/critical
- [ ] Agent health: 1 HP, 0.2/sec damage in critical zone (~5 sec to death)
- [ ] Death counter in GameState (10 = game over)
- [ ] Danger zone visuals: smooth gradient heat map overlay (per-tile, interpolated)
- [ ] Death visual: particle burst in agent's color
- [ ] TAB toggle for density overlay

### Phase 2: Time & Schedule System
- [ ] Game clock in 24-hour format (14:34)
- [ ] 4 phases: Day/Night/Exodus/Dead Hours (3 min each, 12 min total)
- [ ] Artist data: procedural name generation, crowd_size, duration (30-60 min by tier)
- [ ] Spawn rate ramps up ~15 min before each performance
- [ ] Exodus: agents who can't exit stay for next day
- [ ] Stage lights up when artist starts performing

### Phase 3: Entry/Exit Control
- [ ] Pre-built perimeter fence at game start
- [ ] Pre-built gate (can't delete last gate)
- [ ] Fence tool: 1×1 tile, blocks movement
- [ ] Gate tool: 2×1 tile opening
- [ ] Agents spawn on grass outside fence, enter through gates

### Phase 4: Timeline UI (Google Calendar style)
- [ ] 150px sidebar width (matches minimap)
- [ ] Vertical timeline with "NOW" at 20% from top
- [ ] Events as blocks sized by duration
- [ ] Per artist: name, duration, crowd size number
- [ ] Civ-style scroll: auto-follow + manual peek

### Phase 5: Movement & Pathfinding
- [ ] Agent speed: 0.5 tiles/sec on paths, slower on grass
- [ ] Separation radius: 0.25 tiles
- [ ] Pheromone trail system (agents leaving mark tiles with direction)
- [ ] Trail decay over time
- [ ] Async recalculation over several frames
- [ ] "Festival Pathing" overlay with facility filter

### Phase 6: Minimap & Polish
- [ ] 150×150px minimap at bottom of sidebar
- [ ] Shows: paths, buildings, fence, gates, camera viewport
- [ ] NO density on minimap (use TAB overlay)
- [ ] Click minimap to jump camera

### Phase 7: Game Over & Stats
- [ ] Game over at 10 deaths
- [ ] Stats: days survived, max attendees, total served
- [ ] "Try Again" button

---

## Tuning Parameters

| Parameter | Value | Description |
|-----------|-------|-------------|
| `TILE_SIZE` | 32px | Pixels per tile |
| `MAP_SIZE` | 50×50 | Tiles |
| `MAX_AGENTS_PER_TILE` | 40 | 100% density |
| `warning_density` | 50% (20 agents) | Yellow warning |
| `dangerous_density` | 75% (30 agents) | Movement slows |
| `critical_density` | 90% (36 agents) | Crush damage begins |
| `AGENT_HP` | 1 | Dies in ~5 sec |
| `crush_damage_rate` | 0.2/sec | Flat damage in critical zone |
| `MAX_DEATHS` | 10 | Game over threshold |
| `agent_speed` | 0.5 tiles/sec | Base walking speed on paths (1 m/s) |
| `agent_speed_grass` | 0.25 tiles/sec | 50% speed on grass |
| `separation_radius` | 0.25 tiles | How close before push apart |
| `service_time` | 1 sec | Bathroom/Food service (MVP) |
| `phase_length` | 3 min | Real-time per phase |
| `day_cycle` | 12 min | Full 24-hour cycle |
| `patience_timer` | 60-120 sec | Before agent leaves unhappy |
| `watch_duration` | 30-120 sec | Random time at stage |
| `pheromone_lifespan` | 60 sec | How long trails last |
| `pheromone_strength` | 10 | Fresh trail strength (1-10) |
| `pheromone_decay` | 0.17/sec | Decay rate |

---

## What NOT to Build

**NOT a full tycoon management game.** Keep it simpler:

- ❌ No complex menus or multiple windows
- ❌ No spreadsheet-style management screens
- ❌ No deep economic simulation
- ❌ No staff hiring/management
- ❌ No marketing/reputation systems
- ❌ No weather system
- ❌ No genre-based crowd behavior
- ❌ No individual agent emotional states
- ❌ No regulatory inspections
- ❌ No multiple win conditions (just survival)

**Rule**: If it doesn't directly affect density → crush → death, defer it.

---

## Future Ideas (Post-MVP)

Only add after core loop is fun:

- Multiple stages with scheduling choices
- Merch stands, water stations
- One-way paths
- Barrier pieces
- Queue lane upgrades
- Security staff (simple speed bumps)
- Sound effects and music
- Post-game heatmap replay
- Speed controls (2x, 4x)
- Save/load
- Tutorial

---

## Prototype Timeline

| Milestone | Duration |
|-----------|----------|
| First playable build | 1 week |
| MVP feature complete | 4 weeks (1 month) |
| Polish/iteration | 2 weeks |
| **Total** | **~7 weeks** |

## Scope Priorities

**CANNOT CUT**: Agent system (movement, crowd density, crush mechanic) — this is the core

**CAN CUT IF NEEDED**: Minimap, density overlay system. Focus on agents first.

**SCOPE CREEP DANGER**: Polish. Visual effects, animations, juice, "one more tweak" — these consume infinite time. Get core gameplay working first, polish last.

---

*Last updated: Integrated all 50 answered questions from questions.md — full MVP spec complete.*

**See also**: `questions.md` for detailed rationale behind each decision.
