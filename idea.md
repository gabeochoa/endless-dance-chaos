# Endless Dance Chaos — Game Design Document (Core Gameplay)

## High-Level Vision
A minimalist management game about **shaping crowd flow under pressure** at a growing festival.  
The player does not control people directly — they **shape space**, and the crowd reacts.

The game lives at the intersection of:
- Flow management (Mini Metro)
- Emotional pressure
- Emergent crowd behavior

---

## Core Design Pillars
- Few mechanics, deep interaction
- Fully readable at a glance
- Continuous pressure, soft failure
- Emergent complexity from simple rules
- People feel human, but behave like a system

---

# Core Gameplay Mechanic

## One-Sentence Summary
People continuously flow between attractions and facilities along paths you draw, and **stress builds wherever that flow breaks down**, eventually causing collapse.

---

## Core Elements (Only These Matter)

### 1. Attractions (Sources)
- Continuously generate people
- Generation never stops
- Multiple attractions can exist simultaneously

**Rule:**  
If people cannot leave an attraction fast enough, stress accumulates there.

---

### 2. Facilities (Sinks)
- Remove people from the system
- Fixed absorption rate
- Examples: food, bathrooms, rest zones, exits

**Rule:**  
Facilities do not pull people — people must physically reach them.

---

### 3. Paths (Flow Controllers)
- Player-drawn connections between nodes
- Define preferred movement corridors
- Each path has limited capacity

**Rule:**  
Exceeding capacity causes physical congestion, not instant failure.

---

## Stress — The Central Pressure System

Stress replaces traditional “overfill” mechanics.

### How Stress Builds
Stress increases when:
- Attractions accumulate people
- Paths become congested
- People take too long to reach facilities

### Stress Behavior
- Stress is **local**, not global
- Stress **spreads outward** over time
- High-stress areas worsen nearby flow

### Lose Condition
The game ends when **too many areas reach critical stress simultaneously**.

Failure is gradual, visible, and tense — never sudden.

---

# Crowd Simulation (Movement Model)

## Visual Style Inspiration
Characters resemble **RollerCoaster Tycoon guests**:
- Small, readable sprites
- Slightly goofy, human proportions
- Expressive through motion, not UI

They look individual, but function as a crowd.

---

## Movement Model: Boids-Inspired Swarm Behavior

Each person is an agent with **very limited intelligence**.  
Behavior emerges from local forces, not global planning.

The crowd should feel:
- Fluid when calm
- Chaotic when stressed

---

## Minimal Boids Rule Set

### 1. Path Following (Primary Force)
- Agents are attracted to nearby paths
- Once on a path, they align with its direction

**Purpose:**  
Gives the player strong, readable control over flow.

---

### 2. Separation (Density Avoidance)
- Agents avoid overlapping
- Force increases with local density

**Purpose:**  
Creates visible congestion, bulging crowds, and choke points.

---

### 3. Goal Pull (Weak Attraction)
- Agents are gently attracted toward the nearest facility
- This is fuzzy, not exact pathfinding

**Purpose:**  
Prevents perfect efficiency and introduces organic messiness.

---

## Important Constraint
Agents do **not**:
- Choose attractions
- Track individual needs
- Remember past locations

They are **particles with personality**, not full NPCs.

---

# Stress as a Force Modifier

Stress does not add new rules — it **modifies existing movement forces**.

When stress increases:
- Separation force weakens (people tolerate crowding)
- Alignment to paths degrades
- Goal pull becomes erratic
- Movement becomes jittery and less predictable

This keeps the system simple while dramatically changing feel.

---

## Visual Feedback (System-Driven)

All information is conveyed visually — no numbers required.

| State | Visual Expression |
|-----|-------------------|
| Calm | Smooth walking |
| Busy | Faster movement |
| Stressed | Jitter, head turns |
| Panic | Erratic motion, clumping |

Additional cues:
- Color shifts in crowd density
- Subtle sprite animation changes
- Crowd “bulging” at chokepoints

---

# Player Interaction

The player can:
- Draw paths
- Remove paths
- Place new attractions or facilities when prompted

The player cannot:
- Control individuals
- Assign routes
- Adjust crowd behavior directly

**Design Principle:**  
The player shapes the environment, not the crowd.

---

# Why This Mechanic Works

- Extremely simple rule set
- Continuous, readable pressure
- Physical congestion instead of abstract counters
- Emotional failure instead of mathematical failure
- Clearly distinct from transport and logistics games

This is not about efficiency alone —  
it is about **preventing emotional overload**.

---

# Deferred / Non-Core Ideas (Intentionally Excluded)

The following are **explicitly not part of the core mechanic** and should only be layered later:

- Crowd types
- Weather
- Sound bleed
- Festival themes
- Time-of-day cycles
- Upgrades or meta progression
- Scoring systems

---

## Core Mechanic Litmus Test
If a feature does NOT:
- Affect flow
- Affect congestion
- Affect stress

It does not belong in the core game.

---

# Next Steps
- Reduce to a one-screen prototype
- Tune stress spread rate
- Test boids force weights
- Validate readability at high density
- Compare core loop against Power Grid concept

## Tycoon Framing (Gabe, 2026-10-16)

A tycoon game: make as much money as possible running a music festival, but if you aren't careful people die and you get shut down; grow as big as possible. Inspired by RollerCoaster Tycoon, RimWorld and Prison Architect. Build the festival layout → crowds simulate → money flows → safety risk/shutdown. Favor afterhours systems.

## Visual Reference: RollerCoaster Tycoon 2 (Gabe, 2026-10-17 — primary; supersedes Car Park Capital)

RCT2 look: classic 2:1 isometric (dimetric) tiles, fixed 4-way rotation, pixel-art sprites, limited slightly muted palette, terrain with height steps, tiny peeps with thought bubbles, footpaths/queue lines/scenery, floating title-bar windows with tabs (park info, finances, guests, staff, rides), bottom toolbar with cash/date/rating readout, money floaters. Style only — no RCT2 assets.

Secondary vibe (superseded): Car Park Capital (Gabe, 2026-10-17)

Not a repo — a Steam game (Hilko Janssen / MicroProse, Early Access Oct 9 2026): satirical parking-garage tycoon in a '90s Theme Park / RollerCoaster Tycoon / SimCity 2000 look. Use as the VISUAL north star for this game:
- chunky late-'90s isometric/top-down pixel sim look
- bright saturated palette, clear tile grid
- tiny readable agents
- classic tycoon HUD panels and toolbar
- satirical tone in labels/news ticker
Refs: timeextension.com + killscreen.com coverage of Car Park Capital. Don't copy assets; style only.
