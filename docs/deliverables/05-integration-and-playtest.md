# Phase 05: Integration & Playtest (Demo 1 Complete)

**Demo 1E** | Prerequisite: Phase 04 (facilities + needs work)

## Goal

Wire everything together for Demo 1. Track agent count per tile. Display FPS and agent count on screen. Bug fix, tune speeds, verify the "draw paths, watch crowd form, agents use facilities" loop feels right. Performance target: 500 agents.

**Done when:** Smooth playable loop — draw paths, watch crowd form, agents cycle through facilities.

---

## Deliverables

### 1. Per-Tile Agent Count

Create `UpdateTileDensitySystem`:
- Each frame, clear all `tile.agent_count` to 0
- Iterate all agents, convert position to grid coords, increment `tile.agent_count`
- This is the foundation for crush damage (Phase 07)

### 2. Debug HUD

Draw on-screen using Fredoka font:
- **Agent count**: "Agents: 247" (top-left)
- **FPS**: "FPS: 60" (top-right)
- Use raylib `DrawTextEx()` with loaded Fredoka font
- Semi-transparent background behind text for readability
- This is temporary debug display — replaced by proper UI in Demo 4

### 3. Performance Validation

Target: **500 agents at 60 FPS** (no visible frame drops).

Profile and optimize if needed:
- Grid lookups should be O(1) (array indexing)
- Agent density update is O(N) where N = agent count — acceptable
- Greedy neighbor pathfinding is O(1) per agent per frame — fine
- Rendering: batch draw calls where possible

### 4. Speed Tuning

Verify and adjust:
- Agent spawn rate feels right (not too fast, not too slow)
- Path speed vs grass speed difference is visible
- Service time (1 sec) doesn't cause unrealistic queuing
- Need timers produce a natural rhythm of stage → facility → stage

### 5. Bug Fixes

Common issues to check:
- Agents walking off the grid (out of bounds)
- Agents getting stuck in corners or at grid edges
- Multiple agents stacking on exact same position
- Facility absorption not releasing agents properly
- Camera rotation breaking mouse-to-grid conversion

### 6. E2E Tests

**`tests/e2e/05_full_loop.e2e`**:
```
# Test full Demo 1 loop
reset_game
wait 5

# Build paths
draw_path_rect 0 24 33 26
draw_path_rect 19 19 21 27
draw_path_rect 19 29 21 31
wait 5

# Spawn many agents
spawn_agents 0 25 50 stage
wait 300

# Verify agents distributed
assert_agent_count gt 40
screenshot 05_full_loop

# Verify density tracked
get_density 30 25
screenshot 05_density_at_stage
```

**`tests/e2e/05_performance.e2e`**:
```
# Stress test with 500 agents
reset_game
wait 5

draw_path_rect 0 24 33 26
wait 5

spawn_agents 0 25 500 stage
wait 60

assert_agent_count gte 400
screenshot 05_500_agents
```

---

## Acceptance Criteria

- [ ] Agent count per tile updates correctly each frame
- [ ] Debug HUD shows agent count and FPS
- [ ] 500 agents run at 60 FPS
- [ ] Draw paths → agents use them → agents visit facilities → repeat
- [ ] No agents walk off grid or get permanently stuck
- [ ] Spawning, movement, facility service all work together
- [ ] Demo is shippable: someone can watch it and understand "this is a crowd sim on a grid"

## Out of Scope

- Density visualization overlay (Phase 08 / Demo 2C)
- Crush damage (Phase 07)
- Fence/gate (Phase 06)
- Any UI beyond debug HUD

---

## Demo 1 Complete Checklist

At this point, Demo 1 ("Crowd on a Grid") should be fully playable:

- [x] Phase 01: Grid renders, camera works, E2E framework set up
- [x] Phase 02: Paths can be drawn and removed
- [x] Phase 03: Agents spawn, move, prefer paths
- [x] Phase 04: Facilities work, agents cycle through needs
- [x] Phase 05: Everything integrated, performance validated
