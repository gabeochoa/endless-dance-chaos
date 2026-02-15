# Phase 03: Agent Spawning & Movement

**Demo 1C** | Prerequisite: Phase 02 (paths exist on grid)

## Goal

Agents spawn at a fixed edge point, walk toward a pre-placed stage using greedy neighbor pathfinding. Agents prefer paths (faster) over grass (slower). Agents are drawn as 8x8 raylib-primitive "people."

**Done when:** Agents spawn at the edge, visibly prefer paths, and walk to the stage.

---

## Deliverables

### 1. Spawn Point

Single fixed spawn point at grid edge: `(0, 25)` (left edge, center).

Create a `SpawnPoint` component or just hardcode the location for now:

```cpp
constexpr int SPAWN_X = 0;
constexpr int SPAWN_Z = 25;
```

Agents appear at this tile and begin moving.

### 2. Pre-Placed Stage

Stage placed at a fixed offset position: grid `(30, 25)` — slightly right of center, giving room for paths from spawn to stage.

The stage occupies a 4x4 tile footprint. Mark tiles `(30,25)` through `(33,28)` as `TileType::Stage` in the grid.

### 3. Agent Data

Modify existing `Agent` component or create fresh:

```cpp
struct Agent : BaseComponent {
    FacilityType want = FacilityType::Stage;
    int target_grid_x = -1;
    int target_grid_z = -1;
    float speed = 0.5f;  // tiles/sec, modified by terrain

    // Need timers (added in Phase 04)
    float bathroom_timer = 0.f;
    float food_timer = 0.f;
};
```

Keep `Transform` component for position (`vec2 position`).

### 4. Greedy Neighbor Pathfinding

Each tick, agent picks the adjacent tile (4-directional: up/down/left/right) that:
1. Is walkable (not `Fence`)
2. Is closest to target (Manhattan or Euclidean distance to goal tile)
3. Ties broken by: prefer `Path` tiles over `Grass`

```cpp
// Pseudocode
std::pair<int,int> pick_next_tile(int cur_x, int cur_z, int goal_x, int goal_z, const Grid& grid) {
    // Check 4 neighbors
    // Filter out non-walkable
    // Sort by distance to goal
    // Prefer Path over Grass when distance is equal
    // Return best neighbor
}
```

This is NOT A*, NOT BFS. Just look at immediate neighbors and pick the best one. Agents may get stuck — that's acceptable for Demo 1.

### 5. Speed by Terrain

```cpp
constexpr float SPEED_PATH = 0.5f;   // tiles/sec
constexpr float SPEED_GRASS = 0.25f; // tiles/sec (50%)
```

Agent checks current tile type each frame and adjusts speed accordingly. Movement is continuous (float position), not tile-snapping.

### 6. Agent Rendering

Draw agents using raylib primitives to form a tiny 8x8 "person" shape:

```cpp
// Example: simple person shape at screen position
void draw_agent(float screen_x, float screen_y, Color color) {
    // Head (circle, 2px radius)
    DrawCircle(screen_x, screen_y - 6, 2, color);
    // Body (rectangle, 4x4)
    DrawRectangle(screen_x - 2, screen_y - 4, 4, 4, color);
}
```

Single color for now: pick a neutral human-ish color (e.g., warm tan `#D4A574`). Color variety comes later.

### 7. Spawn System

Create `SpawnAgentSystem`:
- Spawns agents at the fixed spawn point on a timer
- Initial spawn rate: 1 agent every 2 seconds
- Each agent gets `want = FacilityType::Stage` and target = stage position

### 8. E2E Commands

| Command | Args | Description |
|---------|------|-------------|
| `set_spawn_rate` | `RATE` | Set agents per second |
| `assert_agent_near` | `X Z RADIUS` | Assert at least one agent within RADIUS tiles of (X,Z) |

**`tests/e2e/03_agent_movement.e2e`**:
```
# Test agent spawning and movement
reset_game
wait 5

# Draw path from spawn to stage
draw_path_rect 0 24 33 26
wait 5

# Spawn agents
spawn_agents 0 25 5 stage
wait 120

# Agents should be near the stage
assert_agent_near 30 25 8
screenshot 03_agents_near_stage
```

**`tests/e2e/03_path_preference.e2e`**:
```
# Test agents prefer paths
reset_game
wait 5

# Only draw partial path
draw_path_rect 0 24 15 26
wait 5

spawn_agents 0 25 3 stage
wait 60
screenshot 03_agents_on_path
```

---

## Existing Code to Leverage

- `Transform` component for position
- `make_agent()` entity maker (modify for new system)
- `IsometricCamera` for world-to-screen rendering
- Grid from Phase 01

## What to Delete

- `PathSignpost` component and `calculate_path_signposts()` — replaced by greedy neighbor
- `AgentSteering` component (path_direction, separation, wander) — replaced by grid-based movement
- Old agent movement systems in `update_systems.cpp`

---

## Acceptance Criteria

- [ ] Agents spawn at `(0, 25)` on a timer
- [ ] Agents move toward the stage at `(30, 25)`
- [ ] Agents move faster on paths (0.5 t/s) than grass (0.25 t/s)
- [ ] Agents are visible as small person-shaped primitives
- [ ] Greedy neighbor picks walkable tiles toward goal
- [ ] Performance: 500 agents without frame drop
- [ ] E2E: `spawn_agents` + `assert_agent_near` work

## Out of Scope

- Facility needs/timers (Phase 04)
- Pheromone pathfinding (Phase 14)
- Multiple spawn points
- Agent colors/variety
- Separation forces
