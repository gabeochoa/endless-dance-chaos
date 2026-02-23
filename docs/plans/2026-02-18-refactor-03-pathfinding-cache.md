# Refactor 03: Cache Pathfinding Results Per Tile Transition

## Problem

`AgentMovementSystem` calls `pick_next_tile()` for every non-stationary agent every frame. `pick_next_tile` evaluates 4 neighbors with pheromone lookups, a random shuffle, and score comparisons. But agents move at 0.25–0.5 tiles/sec, meaning an agent sits within the same tile for 2–4 seconds and calls pathfinding 120–240 times to get the same (or similar) answer.

### Current flow

```
Every frame, for every agent:
  1. world_to_grid(position)           → current tile
  2. if current != target:
       pick_next_tile(cur, target)     → next tile (4-neighbor eval)
  3. Move toward next tile center
```

The stuck-detection logic already tracks tile transitions via `last_grid_x/z`, confirming that agents stay on the same tile for many frames.

## Proposed Fix

Add two fields to `Agent`:

```cpp
int move_target_x = -1;  // next tile we're walking toward
int move_target_z = -1;
```

### Modified flow

```
Every frame, for every agent:
  1. world_to_grid(position) → current tile
  2. if current tile changed since last frame (tile transition):
       pick_next_tile(cur, goal) → new move target
       agent.move_target_x/z = result
  3. Move toward agent.move_target_x/z center
```

Only re-pathfind when:
- The agent enters a new grid cell (tile transition)
- The agent's goal changes (want/target update)
- The agent starts or stops fleeing

### Edge cases

- **First frame after spawn:** `move_target` is (-1,-1), trigger initial pathfinding.
- **Goal changes mid-tile:** `UpdateAgentGoalSystem` sets `agent.target_grid_x/z`. Add a dirty flag or simply invalidate `move_target` when goal changes.
- **Flee override:** When flee logic kicks in, it already sets `next_x/z` directly — this bypasses the cache naturally.
- **Pheromone changes:** Pheromones shift slowly (decay interval = 1.5s). The agent will re-evaluate on the next tile transition, which happens within 2–4 seconds. Acceptable staleness.

## Files to change

- `src/components.h` — Add `move_target_x/z` to `Agent`
- `src/agent_systems.cpp` — Guard `pick_next_tile` call with tile-change check; invalidate on goal change

## Estimated impact

- **Performance:** With 200 agents at 60fps, reduces pathfinding calls from ~12,000/sec to ~100/sec (one call per tile transition per agent). This is a ~99% reduction in pathfinding work.
- **Complexity:** Adds 2 fields and a conditional. Net simplification of the movement loop.

## Risks

- Agents will be slightly less responsive to rapidly changing pheromone fields. In practice, pheromones decay slowly (every 1.5s) and deposit gradually, so this is negligible.
- If the game adds dynamic obstacles (tiles changing type mid-walk), the cached target could point at a now-blocked tile. Guard: if `move_target` is blocked, invalidate and re-pathfind.
