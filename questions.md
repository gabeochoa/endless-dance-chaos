# Endless Dance Chaos - Design Questions

These questions need answers before an intern can implement the prototype. Please answer each one to remove ambiguity from the design.

---

## Grid & Map

### Q1. What is the tile size in pixels?
**ANSWER: 32x32 pixels** (at 1x zoom, 720p)

At 720p this gives ~40x22 tiles visible on screen. Buildings occupy multiple tiles.

### Q2. What is the starting map size in tiles?
**ANSWER: 50x50 tiles** (1600x1600 pixels at 32px/tile)

At 720p (~40x22 visible), player sees about 80% of map width at once when zoomed in. Good starting size for MVP.

### Q3. Can the map be expanded during gameplay, or is it fixed?
**ANSWER: Fixed** — 50x50 tiles for MVP. Map expansion could be a post-MVP feature.

### Q4. What is outside the festival boundary?
**ANSWER: Grass with a pre-built perimeter fence**

- The playable area (50x50) has a **starting perimeter fence** around it
- Outside the fence is grass where agents spawn and queue to enter
- Agents must enter through **gates** the player places in the perimeter fence
- Player builds/modifies everything INSIDE the fence
- The perimeter fence itself can be edited (add gates, move sections)

### Q5. How many agents can occupy a single tile before it's at 100% density?
**ANSWER: 40 agents per tile**

Real-world basis:
- 1 tile = 2m × 2m = 4 m²
- Fire code: 1 person per 0.5 m² (2 people/m²)
- Festival crush density: 5× fire code = 10 people/m²
- 4 m² × 10 people/m² = **40 agents at 100% density**

Density thresholds:
| Level | Agents/Tile | % Capacity |
|-------|-------------|------------|
| Normal | 0-20 | <50% |
| Warning | 20-30 | 50-75% |
| Dangerous | 30-36 | 75-90% |
| Critical | 36-40+ | >90% (crush damage) |

---

## Agent Visuals & Animation

### Q6. What is the sprite size for agents at close zoom?
**ANSWER: 8×8 pixels**

At 40 agents per 32×32 tile at crush density, 8×8 sprites will overlap and pile up visually — creates the "packed crowd" feel. Can adjust later if it feels too abstract.

### Q7. What are the walking animation frames?
**ANSWER: 2 frames** — simple toggle between frames. At 8×8px, more frames wouldn't be noticeable anyway.

### Q8. Do agents have directional sprites (facing left/right/up/down)?
**ANSWER: D) Same sprite always** — no directional facing for MVP. Keeps art requirements minimal. Can add directions post-MVP.

### Q9. At far zoom, what do the "density blobs" look like?
**ANSWER: B) Heat map gradient** — transparent overlay on ground, green→yellow→red based on density. Clear data visualization style, easy to read danger zones at a glance.

### Q10. Describe the "pops like a balloon" death effect:
**ANSWER: D) Simple particle burst** — agent instantly vanishes, replaced by a burst of particles. Quick, punchy, no elaborate animation.

### Q11. What colors are the death particles?
**ANSWER: C) Match the agent's color** — implies agents have varied colors for visual diversity. Death particles inherit their color.

---

## Agent Behavior

### Q12. What is the agent base walking speed in tiles per second?
**ANSWER: 0.5 tiles/sec** (1 m/s real-world)

This is a slow festival shuffle — crowds move slowly. At 50 tiles across the map, it takes 100 seconds to cross. Gives player time to react to developing crushes.

### Q13. What is the agent separation radius (how close before they push apart)?
**ANSWER: A) 0.25 tiles** (8px / 0.5m real-world)

Tight packing — agents can get very close before separation force kicks in. Allows for realistic crowd crushing behavior.

### Q14. When an agent wants food AND wants to see an artist, how do they prioritize?
**ANSWER: B) Needs first if urgent, otherwise artist**

Simple threshold check: if hunger/bathroom need exceeds X%, go handle it first. Otherwise, head to stage. Post-MVP can add more nuanced decision-making.

### Q15. What happens when an agent cannot reach their goal (no path exists)?
**ANSWER: B then C** — Agent wanders randomly for a while, then eventually gives up and exits unhappy.

Post-MVP: Add toast/notification system showing why people are leaving (e.g., "Guest left: couldn't find bathroom")

### Q16. Do agents have a "patience" timer? What happens when it expires?
**ANSWER: B) Yes, they leave unhappy after X seconds**

Agents have limited patience. If stuck/waiting too long, they give up and exit. (Exact timeout TBD — maybe 60-120 seconds?)

### Q17. How long (in real seconds) does an agent typically stay at a stage before leaving?
**ANSWER: D) Random between min/max**

Each agent gets a random watch duration (e.g., 30-120 seconds). Creates natural crowd turnover — some leave early, some stay the whole set. (Exact min/max TBD)

---

## Facilities

### Q18. How large is each facility in tiles? (32x32 per tile)

| Facility | Size | Pixels |
|----------|------|--------|
| Stage | ___x___ tiles | ___x___ px |
| Bathroom | ___x___ tiles | ___x___ px |
| Food Stand | ___x___ tiles | ___x___ px |
| Gate | ___x___ tiles | ___x___ px |
| Fence segment | ___x___ tiles | ___x___ px |
| Path segment | ___x___ tiles | ___x___ px |

**CONFIRMED:**
| Facility | Size | Pixels | Real-world |
|----------|------|--------|------------|
| Stage | 4x4 tiles | 128x128px | 8m × 8m |
| Bathroom | 2x2 tiles | 64x64px | 4m × 4m |
| Food Stand | 2x2 tiles | 64x64px | 4m × 4m |
| Gate | 2x1 tiles | 64x32px | 4m × 2m opening |
| Fence | 1x1 tile | 32x32px | 2m segment |
| Path | 1 tile wide | 32px | 2m wide |

### Q19. What is the service time for each facility (in real seconds)?
**ANSWER: 1 second for both (MVP starting point)**

| Facility | Service Time |
|----------|--------------|
| Bathroom | 1 sec |
| Food Stand | 1 sec |

Fast for now — can tune up if facilities become too efficient at absorbing crowds.

### Q20. What is the queue capacity for each facility?
**ANSWER: Same as path tile density (40 agents)**

Default queue = agents just crowd around facility like any tile.

Post-MVP upgrade: Add formal "queue lane" upgrade → faster service but lower capacity (trade-off between throughput speed vs. buffer size).

### Q21. When a facility queue is full, what happens to arriving agents?
**ANSWER: Depends on urgency**

- **Urgent need (bathroom, high hunger)**: B) Try to find another facility of same type
- **Non-urgent**: C) Give up and wander, maybe try again later

Agents don't just stand there blocking — they make a decision based on how badly they need it.

### Q22. How is the stage "attraction zone" defined?
**ANSWER: Pheromone trail system (BGP-style)**

Agents don't have global knowledge. Instead:
1. Agents leaving a stage mark tiles with "stage is ~X tiles THAT direction"
2. More agents leaving = stronger pheromone trail
3. New agents follow strongest trails toward stages
4. Trails decay over time if not reinforced

Creates emergent crowd flow — popular routes get reinforced, natural bottlenecks form. More complex than BFS signposts but much more interesting behavior.

**"Watching" distance**: Agent counts as watching once within ~8-10 tiles of stage (they stop moving and enjoy the show).

---

## Building & Paths

### Q23. How wide are paths?
**ANSWER: Always 1 tile (32px)**

But multiple adjacent paths merge visually into larger open areas (plazas). Simple placement, flexible results.

### Q24. How does path drawing work with the controller?
**ANSWER: A) Click start → move cursor → click end → fills rectangle**

Most controller-friendly: click corner, move to opposite corner, confirm. Fills rectangle of path tiles. Fast for both long paths and open plazas. Can adjust if it feels wrong.

### Q25. Can paths be placed diagonally?
**ANSWER: A) No, cardinal only** — grid-aligned paths keep visuals clean and simple.

### Q26. What happens when you try to place a building on an occupied tile?
**ANSWER: A) Blocked** — red highlight, build fails. Must demolish first.

### Q27. Can you build while the game is running (unpaused)?
**ANSWER: A) Yes, no restrictions** — live building allowed. React to problems in real-time.

---

## Fence & Gates

### Q28. Is the fence required at game start, or optional?
**ANSWER: A) Default perimeter fence at start** — matches Q4, festival has pre-built boundary.

### Q29. If the player builds no fence/gates, where do agents spawn and enter?
**ANSWER: C) Game starts with one default gate** — like the perimeter fence, there's one gate pre-placed. Agents spawn outside and enter through it. Player can add more gates or move the default one.

### Q30. Can gates have capacity upgrades, or is throughput fixed?
**ANSWER: C) Place multiple gates** — no upgrade system, just add more gates for more throughput. Simple.

### Q31. What happens if all gates are deleted while agents are inside?
**ANSWER: C) Game prevents deleting last gate** — can't soft-lock yourself. Always at least one entry/exit.

---

## Time & Artists

### Q32. How is time displayed in the UI?
**ANSWER: B) 24-hour format** (14:34) — cleaner, no AM/PM ambiguity.

### Q33. How long is each time phase in real minutes?
**ANSWER: Equal length — 3 min each, 12 min total cycle**

| Phase | Real Minutes |
|-------|--------------|
| Day (10am-6pm) | 3 min |
| Night (6pm-Midnight) | 3 min |
| Exodus (Midnight-3am) | 3 min |
| Dead Hours (3am-10am) | 3 min |
| **Total cycle** | **12 min** |

### Q34. How are artist names generated?
**ANSWER: B) Procedurally generated** — prefix + suffix tables (e.g., "DJ" + "Nova", "The" + "Midnight" + "Collective"). Infinite variety, fun names.

### Q35. How long does each artist performance last in game-time?
**ANSWER: C) Variable by tier** — small artists ~30 min, headliners ~1 hour. Bigger acts = longer sustained crowd pressure.

### Q36. During Exodus phase, what happens to agents who can't exit in time?
**ANSWER: A) Stay for next day** — stragglers become part of tomorrow's crowd. Creates carryover consequences for bad exit flow.

### Q37. Do agents arrive throughout the day, or all at once before each performance?
**ANSWER: A + C combined** — continuous stream, but spawn rate ramps up ~15 min before each performance based on artist popularity. Creates predictable waves tied to schedule.

---

## Camera & Controls

### Q38. How many tiles are visible at each zoom level?
**ANSWER: Reasonable defaults (tune as needed)**

| Zoom Level | Visible Width | Notes |
|------------|---------------|-------|
| Close | ~25 tiles | Half map, see agent detail |
| Medium | ~40 tiles | Natural 720p view |
| Far | ~60 tiles | Entire 50x50 map + margin |

### Q39. How fast does the camera pan (tiles per second when holding stick)?
**ANSWER: D) Scales with zoom** — faster when zoomed out, slower when zoomed in. ~10 tiles/sec at medium zoom as baseline.

### Q40. Is there camera momentum/smoothing, or instant stop?
**ANSWER: B) Slight momentum** — small ease-out for polish, but not floaty. Feels responsive.

### Q41. Can the camera go outside the map bounds?
**ANSWER: B) Small padding** — can see slightly past map edge (helpful for placing gates at perimeter), but not unlimited.

---

## UI Specifics

### Q42. What is the sidebar width?
**ANSWER: Same width as minimap** — keeps things aligned. (See Q52 for minimap size, likely ~150-200px)

### Q43. When the timeline has more artists than fit, how does scrolling work?
**ANSWER: Civ-style** — auto-scrolls to keep current/next artist visible, but player can manually scroll to peek ahead. Best of both worlds.

### Q44. What information shows for each artist in the timeline?
**ANSWER: Google Calendar day view style**

Layout:
- Vertical timeline like Google Calendar day view
- "NOW" marker fixed at ~20% from top
- Timeline scrolls upward as time passes
- Events are blocks that span their duration (taller = longer set)

Per artist block:
- ✓ Artist name
- ✓ Duration (block height represents this visually)
- ✓ Expected crowd size (number)

Reference: Google Calendar day view with time slots and event blocks

### Q45. What does the death counter look like when approaching game over?
**ANSWER: Nothing special for MVP** — just a number display. Polish effects (color/pulse/shake) can be added post-MVP.

---

## Danger & Crush Details

### Q46. What is the agent's crush health value?
**ANSWER: C) 1 HP** — dies in ~5 seconds in critical zone. Fast and punishing — forces immediate action.

### Q47. Does crush damage increase with density level?
**ANSWER: A) Flat damage** — 0.2/sec in any critical zone (>90% density). Simple for MVP, can add scaling later.

### Q48. Is there screen shake when agents die?
**ANSWER: A) No shake for MVP** — keep it simple. Juice effects post-MVP.

### Q49. How is the "dangerous zone" overlay rendered?
**ANSWER: B) Gradient** — blends into surroundings, shows density as smooth heat map rather than hard tile edges.

### Q50. Does the danger overlay show for individual tiles or continuous regions?
**ANSWER: Per-tile but smoothly interpolated** — density calculated per tile, but rendered with smooth gradients between tiles so it looks like a continuous heat map, not blocky squares.

---

## Minimap

### Q51. Where is the minimap positioned?
**ANSWER: A) Bottom-right of sidebar** — below the artist timeline.

### Q52. What is the minimap size?
**ANSWER: 150x150 pixels** — current codebase value (MAP_SIZE = 150). Sidebar should match this width.

### Q53. What does the minimap show?
**ANSWER: Map layout only, no people**
- ✓ Paths
- ✓ Buildings (as blocks)
- ✓ Fence
- ✓ Gates
- ✓ Current camera viewport rectangle
- ✗ NO agent density / heat map
- ✗ NO danger zones

Keeps minimap clean. Use TAB overlay on main view for density info.

### Q54. Can the player click/select on the minimap to move the camera?
**ANSWER: A) Yes** — click minimap to jump camera. Standard RTS behavior.

---

## Color Palette Specifics

### Q55. Day palette (Miami pastel):

| Element | Hex Code | Description |
|---------|----------|-------------|
| Background/Ground | #F5E6D3 | Warm sand |
| Grass | #98D4A8 | Soft mint green |
| Path | #E8DDD4 | Light concrete |
| Building Accent 1 | #7ECFC0 | Soft teal |
| Building Accent 2 | #F4A4A4 | Coral pink |
| Stage | #FFD93D | Sunny yellow |
| UI Background | #FFF8F0 | Warm off-white |
| UI Text | #4A4A4A | Soft black |

### Q56. Night palette (EDC neon):

| Element | Hex Code | Description |
|---------|----------|-------------|
| Background/Ground | #1A1A2E | Deep navy |
| Grass | #2D4A3E | Dark forest |
| Path | #2A2A3A | Dark slate |
| Building Accent 1 | #00F5FF | Electric cyan |
| Building Accent 2 | #FF00AA | Hot magenta |
| Stage | #FFE600 | Bright yellow (lights) |
| UI Background | #16162A | Dark purple |
| UI Text | #FFFFFF | White |

### Q57. How does the Day→Night transition work?
**ANSWER: C) Gradual fade over 1 hour game time** — slow, subtle palette shift. Day melts into night naturally.

---

## Audio Hints (for later)

### Q58. What is the audio mood/style you're envisioning?
**ANSWER: Out of scope for MVP** — no audio for now.

Post-MVP vision: Should FEEL like a music festival. Thumping bass you can feel, crowd energy, distant stages bleeding sound. Think: standing outside a festival tent and hearing the muffled drop.

### Q59. Should there be audio cues for danger?
**ANSWER: Out of scope for MVP** — visual only for now.

Post-MVP: Rising tension audio as density climbs, crowd panic sounds, maybe heartbeat SFX at 8+ deaths.

---

## Technical Constraints

### Q60. What is the target frame rate?
**ANSWER: 120 FPS** — smooth high refresh rate target.

### Q61. What is the minimum supported screen resolution?
**ANSWER: A) 1280x720** — matches DEFAULT_SCREEN_WIDTH/HEIGHT in code.

### Q62. At 100k agents, what's acceptable?
**ANSWER: A) Must maintain 60 FPS** — aggressive LOD/culling required. Performance is non-negotiable even at scale.

---

## Game Feel & Juice

### Q63. When placing a building successfully, what feedback occurs?
**ANSWER: E) None for MVP** — instant silent placement. Juice post-MVP.

### Q64. When an artist starts performing, what happens?
**ANSWER: C) Stage lights up / particle effect** — visual feedback that something is happening. Makes the stage feel alive.

### Q65. Is there a "beat" to the gameplay (screen pulses to music)?
**ANSWER: D) No rhythmic elements for MVP** — no audio, no pulse. Post-MVP idea.

---

## Edge Cases

### Q66. What happens if the player places no stage?
**ANSWER: C) Default stage pre-placed** — game starts with one stage (see Q74).

### Q67. What happens if all paths are deleted mid-game?
**ANSWER: A) Agents walk on grass (slower)** — they can still move, just at reduced speed.

### Q68. Can facilities be moved, or only demolished and rebuilt?
**ANSWER: A) Move freely** — pick up and place anywhere. More flexible for the player.

### Q69. If a gate is deleted while agents are walking through it, what happens?
**ANSWER: B) Agents clip through until clear** — simplest solution. Gate just vanishes, agents continue.

### Q70. What's the max number of facilities at each milestone?
**ANSWER: Simple formula — +1 of each type per 100 attendees**

| Attendees | Stages | Bathrooms | Food | Gates |
|-----------|--------|-----------|------|-------|
| Start | 1 | 1 | 1 | 1 |
| 100 | 2 | 2 | 2 | 2 |
| 200 | 3 | 3 | 3 | 3 |
| etc. | +1 | +1 | +1 | +1 |

Simple linear scaling. Can tune later if needed.

---

## Signpost System

### Q71. How often are signposts/pheromones recalculated after path changes?
**ANSWER: B) Async over several frames** — avoid frame drops on big maps.

### Q72. What happens in areas with no signpost/pheromone coverage?
**ANSWER: A) Direct line-to-goal** — agents beeline toward target, may hit walls and get stuck (realistic for lost festival-goer).

### Q73. Do signposts/pheromones show visually in debug mode?
**ANSWER: In-game overlay with facility filter**

"Festival Pathing" overlay mode (toggle):
- Shows arrows on each tile indicating "where would you go from here?"
- Filter by facility type: Stage, Bathroom, Food, Exit
- Arrows show pheromone direction/strength
- Helps player understand crowd flow and debug pathfinding

Not just debug — useful for players to understand why crowds move certain ways.

---

## Starting State

### Q74. What does the festival look like at game start?
**ANSWER: C) Basic template** — perimeter fence + one gate + one stage pre-placed. Player builds paths and facilities from there.

### Q75. Is there a tutorial for the first game?
**ANSWER: D) No help at all for MVP** — learn by doing. Tutorial post-MVP.

---

## Summary

**All 75 questions answered!** This document is now a complete spec for the intern to implement the MVP. Key decisions:

- 32×32px tiles, 50×50 map, 40 agents/tile at crush
- 8×8px agents, 2-frame animation, no directional sprites
- Pheromone trail pathfinding (BGP-style)
- Google Calendar-style timeline
- Pre-built fence + gate + stage at start
- No audio, minimal juice for MVP — focus on core loop

