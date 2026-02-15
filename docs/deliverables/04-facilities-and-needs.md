# Phase 04: Facilities & Needs

**Demo 1D** | Prerequisite: Phase 03 (agents move to stage)

## Goal

Agents have timer-based needs (bathroom, food). Pre-placed facilities service agents. Stage has a semi-circle watching zone. Agents cycle between stage, bathroom, food naturally.

**Done when:** Agents cycle between stage → bathroom → food → stage naturally.

---

## Deliverables

### 1. Need Timers

On spawn, each agent gets random timers:

```cpp
struct AgentNeeds : BaseComponent {
    float bathroom_timer = 0.f;     // counts up, triggers need
    float bathroom_threshold = 0.f; // random 30-90 sec
    float food_timer = 0.f;
    float food_threshold = 0.f;     // random 45-120 sec
    bool needs_bathroom = false;
    bool needs_food = false;
};
```

On spawn, set thresholds to random values. Timers tick each frame. When timer >= threshold, the need activates and agent switches goal.

Priority: `Bathroom > Food > Stage` (urgency-based)

### 2. Stage Watching Zone

Semi-circle zone in front of the stage:
- Radius: 8-10 tiles from stage center
- "Front" = the side facing adjacent path tiles
- Agents entering the zone stop moving and "watch" for a random duration (30-120 seconds)
- After watching, resume need-checking behavior

```cpp
struct WatchingStage : BaseComponent {
    float watch_timer = 0.f;
    float watch_duration = 0.f; // random 30-120 sec
};
```

To determine "front" of stage: scan the 4 edges of the 4x4 stage footprint, find the edge with the most adjacent `Path` tiles. The semi-circle extends from that edge.

### 3. Pre-Placed Facilities

At game start, place:
- **1 Bathroom** (2x2 tiles) at grid `(20, 20)` — mark tiles as `TileType::Bathroom`
- **1 Food** (2x2 tiles) at grid `(20, 30)` — mark tiles as `TileType::Food`
- Stage already placed in Phase 03 at `(30, 25)`

### 4. Facility Service

When agent reaches a facility tile:
- Agent entity is hidden/absorbed (removed from world rendering, position stored)
- After `SERVICE_TIME` (1 second), agent reappears adjacent to facility
- Need flag is cleared, timer resets with new random threshold
- Agent picks next goal based on priority

```cpp
constexpr float SERVICE_TIME = 1.0f; // seconds

struct BeingServiced : BaseComponent {
    int facility_grid_x = 0;
    int facility_grid_z = 0;
    FacilityType facility_type;
    float time_remaining = SERVICE_TIME;
};
```

### 5. Facility Capacity (Density-Based)

Facility "fullness" is based on tile density:
- Facility tiles track `agent_count` just like all tiles
- When density on facility tiles exceeds capacity threshold, facility is "full"
- Full facility: agent tries next 2-3 closest facilities of same type
- If all full: urgent need (bathroom) keeps searching, non-urgent (food) gives up and wanders

```cpp
constexpr int FACILITY_MAX_AGENTS = 20; // agents on facility tiles before "full"
```

### 6. Goal Selection System

Create `UpdateAgentGoalSystem`:
- Check need timers each frame
- If `needs_bathroom`: target nearest bathroom
- Else if `needs_food`: target nearest food
- Else: target stage (go watch)
- "Nearest" = shortest Manhattan distance on the grid

### 7. E2E Commands

| Command | Args | Description |
|---------|------|-------------|
| `set_agent_need` | `AGENT_ID TYPE` | Force an agent to need bathroom/food |
| `assert_agents_at_facility` | `TYPE OP COUNT` | Assert count of agents at facility type |
| `assert_agent_watching` | `COUNT` | Assert COUNT agents are in watching state |

**`tests/e2e/04_needs_cycle.e2e`**:
```
# Test agent need cycle
reset_game
wait 5

# Set up paths
draw_path_rect 0 24 33 26
draw_path_rect 19 19 21 27
draw_path_rect 19 29 21 31
wait 5

# Spawn agents
spawn_agents 0 25 10 stage
wait 300

# Some agents should be at facilities by now
screenshot 04_agents_cycling
```

**`tests/e2e/04_facility_service.e2e`**:
```
# Test facility absorption
reset_game
wait 5

# Spawn agent directly at facility
spawn_agent 20 20 bathroom
wait 30

# Agent should be serviced and gone
screenshot 04_after_service
```

---

## Existing Code to Leverage

- `Facility` component (modify for new system)
- `FacilityType` enum
- `Agent` component
- Grid from Phase 01
- Movement from Phase 03

## What to Delete

- `InsideFacility` component → replaced by `BeingServiced`
- `LingeringAtStage` → replaced by `WatchingStage`
- `AgentTarget` → folded into `Agent` component

---

## Acceptance Criteria

- [ ] Agents watch stage for random duration (30-120s)
- [ ] Bathroom need triggers on timer, agent walks to bathroom
- [ ] Food need triggers on timer, agent walks to food
- [ ] Priority: bathroom > food > stage
- [ ] Agents are absorbed during service (1 sec), then reappear
- [ ] Full facility: agent tries next 2-3 closest
- [ ] Semi-circle watching zone works (agents stop in front of stage)
- [ ] Agents cycle naturally without getting permanently stuck

## Out of Scope

- Pheromone pathfinding (Phase 14)
- Patience timer / agent leaving (Phase 15)
- Multiple stages
- Facility building by player (Phase 16)
