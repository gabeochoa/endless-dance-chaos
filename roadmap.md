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

## Visual Style: RollerCoaster Tycoon 2 Style

### Isometric View
- **Camera**: RCT-style isometric with 4 rotations (90° increments)
- **Zoom**: 3 levels (close, medium, far)
- **Rotation**: L/R triggers cycle through the 4 angles

### Color Palette
- **Day (10am-6pm)**: Miami pastel - soft pinks, teals, yellows
- **Evening/Night (6pm-3am)**: EDC neon - electric blues, purples, magentas
- **Transition**: Gradual palette shift as time passes

### Attendee Visuals (LOD System)
Because we're targeting 100k attendees:

| Zoom Level | Visual |
|------------|--------|
| Close | Small sprites with basic animation (2-4 frames walking) |
| Medium | Simplified sprites, batch rendering |
| Far | Abstract density blobs, heat-map style coloring |

### Path & Building Style
- **Paths**: RCT-style connected tiles, clear texture
- **Facilities**: Isometric buildings with signage
- **Fence**: Simple dark line barrier

### Danger Visualization
- **Default**: Only critical (red) zones visible on main view
- **Density Overlay (hotkey)**: Full heat-map showing green→yellow→red
- **Minimap**: Always shows density hotspots
- **Crush Event**: Pulsing red, particles, agent "pops like a balloon"

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

| Phase | Time | What Happens |
|-------|------|--------------|
| **Day** | 10am - 6pm | Artists perform, moderate crowds |
| **Night** | 6pm - Midnight | Headliners, largest crowds, peak danger |
| **Exodus** | Midnight - 3am | ALL attendees must exit. Exits become bottlenecks. Deaths still count! |
| **Dead Hours** | 3am - 10am | Empty festival. Build/reorganize in peace. |

### Time Controls
- **Spacebar**: Full pause - freezes time but allows building/camera movement
- **Escape**: Pause menu
- **Speed**: 1x only for MVP (2x/4x later)

### Day Length
- **MVP**: ~10-15 real minutes per full cycle
- **Later**: Tunable based on playtesting

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

1. Artists are pre-generated for the day based on current max attendance
2. As festival grows (more total attendees), bigger artists appear
3. Small artists → Small crowds, Big artists → Big crowds
4. Timeline shows what's coming so player can prepare

### Spawning & Flow
- **Agents spawn at map edge** but must enter through player-placed **Gates** in the **Fence**
- On spawn, agent has:
  - Target artist they want to see
  - Optional secondary needs (food, bathroom, merch)
- After seeing their artist, agents are happy to leave
- If stress gets too high, agents leave early (unhappy)

---

## Core Systems

### 1. The Crush Mechanic (Primary Danger)

| Density Level | Visual | Effect |
|--------------|--------|--------|
| Normal (< 50%) | Green | Safe |
| Warning (50-75%) | Yellow | Caution |
| Dangerous (75-90%) | Orange pulse | Movement slows |
| Critical (> 90%) | Red pulse, shake | Crush damage begins |

**Crush Damage**:
- Agents in critical density take continuous damage
- Health depletes → Agent dies → Counter increments
- **10 deaths = Game Over**

**Death Visual**: Agent "pops like a balloon" (particle burst)

### 2. Agent Movement (Boids-Inspired)

Agents move via local forces, not global pathfinding:

| Force | Description |
|-------|-------------|
| **Goal Pull** | Move toward current target (stage, exit, facility) |
| **Signpost Following** | Read path signposts for direction hints |
| **Separation** | Push away from nearby agents |
| **Path Preference** | Prefer paths over grass |

**Movement Speeds**:
- **Paths**: Full speed
- **Grass**: Slower (if no fence blocking)
- **Fence**: Blocks movement completely

### 3. Signpost Pathfinding

Paths have embedded signposts (BFS-calculated) pointing toward facilities:
- Agents check current tile's signpost
- Signpost says "bathroom is NORTH"
- Agent gets steering force toward that direction
- Signposts recalculate when paths change

### 4. Facilities

| Facility | Effect | Strategy Use |
|----------|--------|--------------|
| **Stage** | Attracts agents for performances | Primary destination |
| **Bathroom** | Slow service, absorbs agents | Delay crowds |
| **Food** | Medium service, absorbs agents | Spread crowd |
| **Gate** | Entry/exit point in fence | Control flow in/out |
| **Fence** | Blocks all movement | Define festival boundary |

**Absorption Mechanic**:
- Agent enters facility queue (removed from world)
- After service time, agent exits
- Queue capacity limits how many can wait

**Queue Management** (Later):
- Queue lanes as facility upgrade, or
- Player-drawn queue barriers, or
- Hybrid queue-path type

### 5. Festival Progression

Festival grows based on **max attendees ever in park**:

| Milestone | Unlock |
|-----------|--------|
| 100 attendees | +1 facility slot |
| 250 attendees | +1 facility slot |
| 500 attendees | +1 stage slot (later) |
| etc. | etc. |

More slots = bigger festivals = bigger artists = more danger.

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
- **Facilities**: Limited by unlocked slots
- **Timing**: Build any time (paused or live)
- **Cost**: None for MVP
- **Movement**: Can relocate stages and facilities freely

### Demolition
- Instant removal
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

### Top Bar
- Current time and day
- Death counter (prominent: "Deaths: 3/10")
- Attendees currently in park
- Resources (if any)

### Right Sidebar (Collapsible)
- **Timeline**: Scrolling list of upcoming artists with crowd sizes
- **Minimap**: Shows density overlay at all times

### Build Bar (Bottom)
- Tool icons
- Current tool highlighted
- Cycle with L/R bumpers

---

## Game States

### Main Menu
Simple: New Game, Options, Quit

### Playing
Normal gameplay with pause (Spacebar) and pause menu (Escape)

### Game Over
Triggered at 10 deaths.

**Game Over Screen**:
- "FESTIVAL SHUT DOWN"
- Stats: Days survived, max attendees, total served
- Heatmap of death locations (later)
- "Try Again" button

### Victory
**Endless mode** - no formal victory. Goal is survival and growth.

---

## The Emotional Loop

### Success Feeling
"Damn, I just survived a 2000-person headliner rush with zero deaths. My path layout worked perfectly. Like beating a boss in tower defense - held my breath the whole time."

### Failure Feeling
"I was SO close! Juggling all those balls, spinning all those plates... but I know exactly what went wrong. That bottleneck near the stage killed me. Next time I'll widen that path BEFORE the headliner."

---

## Visual References

- **Mini Motorways** (minimalist flow, emergent complexity)
- **RollerCoaster Tycoon 2** (isometric view, nostalgic charm)
- **Train Valley 1 & 2** (path drawing, timing challenge)
- **Package Inc** (logistics management)
- **Freeways** (road drawing)
- **Breaking the Tower (Notch)** (creative tower defense)

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

### OUT OF MVP SCOPE
- Sound effects / Music
- Tutorial
- Save / Load
- Speed controls (2x, 4x)
- Multiple stages
- Merch / Water facilities
- Weather system
- Staff/security units
- Heatmap replay
- Complex management menus

---

## Implementation Checklist

### Phase 1: Core Crush Mechanic
- [ ] Track agent density per tile
- [ ] Define density thresholds (warning, dangerous, critical)
- [ ] Add CrushHealth component to agents
- [ ] CrushDamageSystem: damage agents in critical zones
- [ ] Death counter in GameState
- [ ] Game over at 10 deaths
- [ ] Danger zone visuals (red overlay on critical tiles)
- [ ] Death visual: agent pops like balloon
- [ ] Density overlay toggle (hotkey)
- [ ] Minimap shows density at all times

### Phase 2: Time & Schedule System
- [ ] Game clock (time of day, not just elapsed)
- [ ] Define time phases (Day, Night, Exodus, Dead Hours)
- [ ] Artist data structure (name, crowd_size, start_time, duration)
- [ ] Pre-generate daily lineup based on festival size
- [ ] Spawn rate scales with current artist popularity
- [ ] Exodus phase: all agents pathfind to exits
- [ ] Dead hours: no spawns, rebuild time

### Phase 3: Entry/Exit Control
- [ ] Fence building tool (blocks movement)
- [ ] Gate building tool (place in fence)
- [ ] Agents spawn at map edge, must enter through gates
- [ ] Gate capacity affects entry speed (bottleneck)
- [ ] Exit flow during exodus phase

### Phase 4: Timeline UI
- [ ] Right sidebar with collapsible panel
- [ ] Current time + day display (top bar)
- [ ] Scrolling artist list with crowd sizes
- [ ] "NOW PLAYING" indicator
- [ ] Visual scaling (bigger crowd = taller entry)
- [ ] Exodus and Dead Hours in timeline

### Phase 5: Movement Polish
- [ ] Movement speed difference: paths fast, grass slow
- [ ] If no fence, agents can walk on grass
- [ ] Physical slowdown in high-density areas
- [ ] Separation force tuning

### Phase 6: Game Over & Stats
- [ ] Game over screen design
- [ ] Stats: days survived, max attendees, total served
- [ ] Death locations summary
- [ ] "Try Again" button

---

## Tuning Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `warning_density` | 50% | When yellow warning appears |
| `dangerous_density` | 75% | When movement slows |
| `critical_density` | 90% | When crush damage begins |
| `crush_damage_rate` | 0.2/sec | How fast agents die in crush |
| `MAX_DEATHS` | 10 | Game over threshold |
| `path_speed` | 1.0 | Full movement speed |
| `grass_speed` | 0.5 | Slower off-path movement |
| `gate_throughput` | 10/sec | How fast agents enter through gate |
| `day_length_minutes` | 15 | Real-time minutes per game day |

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

*Last updated: Based on 65-question design spec from questions.md*
