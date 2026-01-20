# Endless Dance Chaos - Clarification Questions

These questions need answers before an intern can begin prototyping. Please answer each one to make the spec as prescriptive as possible.

---

## Visual Style & Art

### Q1: Isometric Projection Angle
What is the exact isometric projection ratio?
- [x] A) Standard 2:1 isometric (common in classic games like RCT2)
- [ ] B) True isometric (30° from horizontal)
- [ ] C) Dimetric (like Diablo 2, steeper angle)
- [ ] D) Something else: _______

### Q2: Agent Color Palette
The roadmap mentions "varied colors for crowd diversity." How many agent colors should there be, and what are the specific hex values?
- [ ] A) 5 colors (e.g., red, blue, green, yellow, purple)
- [ ] B) 8 colors (wider variety)
- [ ] C) 12+ colors (high diversity)

**ANSWERED:** Agents should look like people (natural/human appearance). Use skin tones and realistic clothing colors rather than arbitrary bright colors. The "diversity" refers to varied but natural-looking festival-goer appearances.

### Q3: Cursor Visual Design
What does the placement cursor look like?
- [ ] A) Simple white square outline (1px border)
- [x] B) Highlighted tile with semi-transparent fill
- [ ] C) Pulsing/animated selection indicator
- [ ] D) Different cursor per tool type

What color should valid placement be? _______ (hex)
What color should invalid placement be? _______ (hex)

### Q4: Facility Sprites
Should facilities have:
- [x] A) Static sprites only (no animation) — for MVP
- [ ] B) Idle animations (subtle movement)
- [ ] C) State-based animations (e.g., bathroom when occupied vs empty)

Describe the visual appearance for each facility:
- **Stage**: _______
- **Bathroom**: _______
- **Food**: _______
- **Gate**: _______
- **Fence segment**: _______

### Q5: Stage "Lights Up" Effect
When an artist starts performing, the stage "lights up." What exactly does this mean?
- [x] A) Color tint overlay on stage sprite — simple for MVP
- [ ] B) Particle effects emanating from stage
- [ ] C) Pulsing glow around stage
- [ ] D) Spotlight beams (simple rays)
- [ ] E) All of the above

**ANSWERED:** Keep it simple for MVP — just a color tint overlay.

### Q6: Death Particle Effect
The death effect is a "particle burst in agent's color." Specify:
- Number of particles: 5-8 (simple burst)
- Particle size: 2-4px (small squares)
- Burst radius: 8-12px
- Duration: 0.3-0.5 sec (quick pop)
- Shape: [x] Square [ ] Circle [ ] Star

**ANSWERED:** Keep it simple for MVP — small square particles, quick burst, nothing fancy.

### Q7: Heat Map Gradient Colors
For the density overlay, what are the exact gradient colors?
```
0% density:   Transparent (no overlay)
50% density:  Yellow (#FFFF00)
75% density:  Orange (#FFA500)
90% density:  Red (#FF0000)
100% density: Black (#000000)
```
**ANSWERED:** Gradient goes transparent → yellow → orange → red → black as density increases.

---

## UI/UX Details

### Q8: Font Selection
What font family should be used?
- [ ] A) Pixel font (retro aesthetic, matching RCT2 vibe)
- [x] B) Clean sans-serif (modern, readable)
- [ ] C) Custom font: _______

**ANSWERED:** Use **Fredoka** (`Fredoka-VariableFont_wdth,wght.ttf` from wm_afterhours2). It's a friendly rounded sans-serif that's modern and readable.

Font sizes:
- Top bar text: 16px
- Timeline artist names: 14px
- Timeline crowd numbers: 12px
- Build bar labels (if any): 12px

### Q9: Build Bar Icons
What style should the build tool icons be?
- [ ] A) Pixel art icons (16x16 or 24x24)
- [x] B) Simple flat icons
- [ ] C) Outlined/stroke icons
- [ ] D) Text labels only

What size should each icon be? _______

### Q10: Tool Selection Indicator
How should the currently selected build tool be indicated?
- [x] A) Highlight box around icon
- [x] B) Icon enlarges/scales up
- [ ] C) Underline bar
- [ ] D) Different background color

**ANSWERED:** Both — highlight box AND icon scales up slightly when selected.

What color for selected state? _______
What color for unselected state? _______

### Q11: Timeline Block Sizing
How tall should each minute of game time be in the timeline (in pixels)? **2px per minute**

Example: A 30-minute set = 60px tall, a 1-hour headliner = 120px tall.

**ANSWERED:** 2px per game minute keeps the timeline compact.

### Q12: Timeline Hover State
When hovering over an artist in the timeline, what happens?
- [x] A) Nothing (no hover state) — MVP
- [ ] B) Tooltip with more details
- [ ] C) Artist block expands to show more info
- [ ] D) Camera jumps to associated stage

### Q13: Minimap Click Behavior
When clicking on the minimap:
- [x] A) Instant camera jump
- [ ] B) Smooth camera pan to location
- [x] C) Drag to pan continuously

**ANSWERED:** Click = instant jump, drag = continuous panning.

### Q14: Death Counter Visual Treatment
When a death occurs, how is the player notified?
- [x] A) Death counter increments only (subtle) — MVP
- [ ] B) Counter flashes red briefly
- [ ] C) Full-screen red vignette flash
- [ ] D) Pop-up notification (e.g., "CROWD CRUSH - 3/10")
- [ ] E) Sound only (no visual beyond counter)

### Q15: Pause Menu Contents
What options should be in the Escape pause menu?
- [x] Resume
- [x] Restart
- [x] Options (volume, etc.)
- [x] Main Menu
- [x] Quit

**ANSWERED:** All of the above.

Any other options? _______

---

## Gameplay Mechanics

### Q16: Grass Movement Speed
Agents move at 0.5 tiles/sec on paths. How fast on grass?
- [x] A) 0.25 tiles/sec (50% speed)
- [ ] B) 0.375 tiles/sec (75% speed)
- [ ] C) 0.4 tiles/sec (80% speed)
- [ ] D) Other: _______

### Q17: Stage Watching Zone Shape
The stage watching zone is ~8-10 tiles. What shape is it?
- [ ] A) Square (8×8 tiles in front of stage)
- [ ] B) Circle (radius from stage center)
- [x] C) Semi-circle (in front direction only)
- [ ] D) Custom trapezoid (wider at back)

Which direction is "front" of the stage? 

**ANSWERED:** The side touching the path. Stage "faces" toward adjacent path tiles.

### Q18: Agent Goal Selection Priority
When an agent enters the festival, how do they pick their target?
- [ ] A) Always go to stage first, then needs
- [ ] B) Random: 70% stage, 30% immediate need (food/bathroom)
- [x] C) Urgency-based (bathroom > food > stage)
- [ ] D) Other priority system: _______

**ANSWERED:** Urgency-based, BUT agents also avoid visibly crowded areas if possible. If a destination looks too packed, they'll try alternatives or wait.

### Q19: Facility Search Radius
When an agent needs a bathroom but the nearest one is full, how far do they search for another?
- [ ] A) Entire map
- [ ] B) Within X tiles: _______
- [x] C) Next 2-3 closest of same type
- [ ] D) Give up immediately

**ANSWERED:** Try next 2-3 closest of same type. (May need tuning during playtesting.)

### Q20: Pheromone System Details
How long does a pheromone trail last before fully decaying? **60 seconds**
How strong is a fresh pheromone mark (1-10 scale)? **10**
How fast does it decay per second? **~0.17** (loses 1 point every 6 sec)

**ANSWERED:** Starting values — will tune during playtesting.

### Q21: "Dangerous" Zone Movement Slowdown
At 75-90% density (dangerous zone), movement slows. By how much?
- [ ] A) 50% speed reduction
- [ ] B) 25% speed reduction
- [x] C) Linear scaling (more crowded = slower)
- [ ] D) Other: _______

**ANSWERED:** Linear scaling — speed decreases proportionally with density.

### Q22: Patience Timer Trigger
When does an agent's patience timer (60-120s) start counting?
- [ ] A) On spawn
- [x] B) When they can't find a path to their goal
- [ ] C) When stuck in one place for X seconds
- [ ] D) When in a crowded area

**ANSWERED:** Starts when pathing fails. (May revisit during playtesting.)

What happens when patience runs out?
- [ ] A) Agent walks straight to nearest exit
- [ ] B) Agent despawns immediately
- [ ] C) Agent becomes "angry" (visual change) then exits
- [ ] D) Agent wanders randomly then exits

**TBD:** Decide during implementation/playtesting. For now, default to A (walk to exit).

### Q23: Agent Stuck Behavior
If an agent literally cannot path to their goal (completely blocked by fences), what happens?
- [ ] A) They wait at the blockage forever
- [x] B) They wander randomly
- [ ] C) They exit the festival (counting as "unhappy")
- [ ] D) They despawn after patience timer

---

## Progression & Balance

### Q24: Initial Spawn Rate
At game start (0 attendees ever), how many agents spawn per minute? 

**ANSWERED:** Based on artists scheduled — spawn rate determined by upcoming artist's expected crowd size.

### Q25: Spawn Rate Scaling
How does spawn rate increase as the festival grows?
- [ ] A) Linear: +X agents/min per 100 max attendees
- [ ] B) Exponential curve
- [ ] C) Step function (big jumps at milestones)
- [x] D) Tied to upcoming artist crowd size

**ANSWERED:** Spawn rate scales with upcoming artist's crowd size. Bigger artists = faster spawning leading up to their set.

### Q26: Artist Tier System
How many tiers of artists are there?
- [ ] A) 3 tiers (Small, Medium, Headliner)
- [ ] B) 5 tiers (more granularity)
- [x] C) Continuous scaling (no discrete tiers)

**ANSWERED:** Continuous scaling — artist crowd sizes scale based on festival progression. No hard tier boundaries. Crowd size and set duration grow gradually as festival grows.

Example ranges (will tune):
| Festival Size | Typical Crowd | Set Duration |
|---------------|---------------|--------------|
| Early (0-500) | 50-200        | 30 min       |
| Mid (500-2k)  | 200-800       | 30-45 min    |
| Late (2k+)    | 500-2000+     | 45-60 min    |

### Q27: Artist Name Generation
For procedural artist names, provide the prefix and suffix tables:

**ANSWERED:** Keep it simple for MVP — just use `"Artist XXX"` where XXX is a random 3-digit number (e.g., "Artist 472", "Artist 831").

Can add fancy name generation (DJ Nova, The Midnight Collective, etc.) post-MVP.

### Q28: Schedule Generation Algorithm
How are artists scheduled throughout the day?
- [x] A) Fixed slots (every 2 hours) — MVP
- [ ] B) Random with minimum gap
- [ ] C) Player-controlled schedule
- [ ] D) Auto-generated based on festival size

How many artists perform per day cycle? **~4 artists** (Day phase 10am-6pm = 8 hours, one every 2 hours)
Minimum gap between performances? **1 hour** (2 hour slots, 1 hour set = 1 hour gap)

**ANSWERED:** Fixed 2-hour slots with 1-hour performances for MVP.

---

## Technical Implementation

### Q29: Camera Rotation Implementation
When rotating the camera 90°:
- [x] A) Instant snap — MVP
- [ ] B) Smooth animated rotation over X ms: _______
- [ ] C) Cross-fade between views

Should map orientation text appear (e.g., "N" indicator)? [x] Yes [ ] No

### Q30: Camera Bounds Padding
"Small padding past map edge allowed." How many tiles? **10 tiles**

### Q31: Path Drawing Mechanics
Step-by-step, how does path drawing work with a controller?
1. Press A to start drawing at current cursor position
2. Move cursor to define rectangle corner
3. Press A again to confirm, B to cancel
4. Yes — preview shows highlighted rectangle before confirmation

**ANSWERED:** Simple rectangle drag: A to start → move cursor → A to confirm (B to cancel). Preview shown during drag.

### Q32: Facility Deletion Behavior
If you delete a facility (bathroom/food) while agents are using it:
- [ ] A) Agents are ejected to nearby tile
- [ ] B) Agents finish using it, then it disappears
- [ ] C) Agents are ejected and go to next facility
- [ ] D) Deletion is blocked while in use

### Q33: Can You Delete the Stage?
- [ ] A) Yes, freely deletable
- [ ] B) No, stage is permanent like last gate
- [ ] C) Can delete if no artist is performing
- [ ] D) Can delete but current artist's crowd becomes angry

### Q34: Building During Different Phases
Can the player build during each phase?
| Phase      | Building Allowed? |
|------------|-------------------|
| Day        | [ ] Yes [ ] No    |
| Night      | [ ] Yes [ ] No    |
| Exodus     | [ ] Yes [ ] No    |
| Dead Hours | [ ] Yes [ ] No    |

Can you build while paused? [ ] Yes [ ] No

---

## Edge Cases & Polish

### Q35: Game Over Transition
When the 10th death occurs:
- [ ] A) Immediate freeze, Game Over screen appears
- [x] B) Brief pause (2-3 sec) showing the death, then transition
- [ ] C) Fade to black, then Game Over screen
- [ ] D) Game continues for a few seconds (chaos) then ends

### Q36: Exodus Phase - Agents Can't Exit
If agents get stuck during exodus and can't exit:
- [ ] A) They teleport outside when Dead Hours begin
- [x] B) They remain inside (carryover as stated) — MVP
- [ ] C) They continue trying to exit through Dead Hours
- [ ] D) They count as "unhappy" but despawn

Do stuck agents count toward next day's attendee count?

### Q37: Empty Festival State
During Dead Hours (3am-10am):
- [ ] A) Player has full control, screen is dimmed
- [ ] B) Time fast-forwards automatically
- [ ] C) Players can only build, no other actions
- [x] D) Standard gameplay, just no agents

### Q38: Multiple Agents Dying Simultaneously
If 5+ agents die in the same frame from a crush:
- [ ] A) Each death triggers its own particle effect
- [x] B) Merge into one bigger explosion
- [ ] C) Stagger the death animations over a few frames
- [ ] D) Show count popup ("x5 deaths!")

### Q39: Performance Target Validation
With 100k agents at 60 FPS, what's the acceptable frame budget?
- Logic update per frame: **~8 ms** max
- Render per frame: **~8 ms** max

Should agents be updated in batches across frames? [x] Yes [ ] No
If yes, how many agents per batch? **TBD — tune during development**

**ANSWERED:** 50/50 split (~8ms each) for 16ms total frame budget. Batching is fine as long as the user can't perceive any stutter or pop-in. Will tune later.

---

## Misc/Additional Details

### Q40: Sound Priority (Post-MVP Prep)
Even though sound is post-MVP, what style should we design toward?
- [ ] A) Realistic festival sounds (crowd noise, music muffled in distance)
- [ ] B) Retro chip-tune style (matching pixel art)
- [x] C) Minimalist ambient (like Mini Metro)
- [ ] D) No preference, decide later

### Q41: Colorblind Accessibility
Should the density overlay have alternative color schemes for colorblindness?
- [ ] A) Yes, include deuteranopia/protanopia-friendly palette
- [x] B) No, not for MVP
- [ ] C) Use symbols/patterns in addition to colors

**ANSWERED:** Not for MVP, but post-MVP the overlay can have patterns added for accessibility.

### Q42: Screen Resolution Scaling
At resolutions higher than 720p:
- [ ] A) Pixel-perfect scaling (2x, 3x, 4x integer only)
- [ ] B) Smooth scaling to fill screen
- [x] C) Fixed game window, black bars for excess space (letterboxing)
- [ ] D) More tiles visible at higher resolutions

**ANSWERED:** Letterboxing (like kart-afterhours) for MVP.

### Q43: Facility Placement Validation Messages
When placement is invalid, should there be text feedback?
- [x] A) Just red highlight, no text
- [ ] B) Small tooltip: "Tile occupied" / "Cannot place here"
- [ ] C) Bottom bar message area
- [ ] D) Error sound only

### Q44: Agent Entry Animation
When agents spawn and enter through gates:
- [ ] A) Pop into existence at gate
- [x] B) Fade in near gate
- [ ] C) Walk in from off-screen edge
- [x] D) Appear in a "bus stop" area outside fence, then walk to gate

**ANSWERED:** Either B or D works — intern can pick. D is more realistic, B is simpler.

### Q45: Winning/High Score
Since there's no victory, is there a high score/leaderboard system?
- [ ] A) Local high score only (best run stats)
- [x] B) No tracking, each run is fresh — MVP
- [ ] C) Stats saved per run for history view
- [ ] D) Unlock cosmetics at milestones

**ANSWERED:** No tracking for MVP. Post-MVP: add local high score saving.

---

## Final Open Questions

### Q46: What's the single most important "feel" the game should have?
(Open answer - write a sentence or two)

**ANSWERED:** Spinning plates until it all comes crashing down. The tension of juggling multiple growing problems, knowing eventually something will slip — and that glorious/tragic moment when it finally does.

### Q47: If the intern has to cut scope, what's the ONE feature that cannot be cut?

**ANSWERED:** The agent system (movement, crowd density, crush mechanic) is the core — cannot be cut. 

**Can be cut if needed:** Minimap, density overlay system. Focus on agents first.

_______

### Q48: What's the ONE feature most likely to cause scope creep that we should be strict about?

**ANSWERED:** Polish. Visual effects, animations, juice, "one more tweak" — these can consume infinite time. Get core gameplay working first, polish last.

_______

### Q49: Are there any visual references (screenshots, videos) the intern should study beyond the game names listed?

**ANSWERED:** Breaking the Tower by Notch — study this one closely for the "creative tower defense" feel and emergent chaos.

_______

### Q50: What's the expected prototype timeline?
- First playable build: **1 week**
- MVP feature complete: **4 weeks** (1 month)
- Polish/iteration: **2 weeks**

**Total: ~7 weeks**

---

*Please answer all questions above. For multiple choice, check the box [x]. For fill-in, write your answer on the blank line.*

