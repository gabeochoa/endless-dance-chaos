# Phase 15: Exodus & Carryover (Demo 3 Complete)

**Demo 3E** | Prerequisite: Phase 14 (pheromone trails work)

## Goal

During Exodus phase, gates emit strong exit pheromone. All agents follow pheromone toward gates and exit. Agents who can't exit by Dead Hours become carryover with a penalty metric.

**Done when:** The exodus creates its own rush — gates become chokepoints, deaths are possible.

---

## Deliverables

### 1. Exodus Pheromone Emission

During Exodus phase (midnight - 3am), gates continuously emit strong exit pheromone:

```cpp
// In a new ExodusPheromonesSystem:
if (clock.get_phase() == GameClock::Phase::Exodus) {
    // Find all gate tiles
    for each gate tile (x, z):
        grid.at(x, z).pheromone[Tile::PHERO_EXIT] = 255; // max strength

    // Also: flood-fill exit pheromone outward from gates each frame
    // This creates a gradient that agents follow naturally
}
```

The exit pheromone creates a gradient: strongest at gates, weaker farther away. Agents follow the gradient toward the nearest gate.

### 2. Pheromone-Driven Exit Behavior

During Exodus, agents naturally switch to following exit pheromone:
- Agent's pathfinding checks exit pheromone channel first
- If exit pheromone exists on adjacent tiles, follow it (highest value)
- If no exit pheromone, beeline toward nearest gate position

No explicit state change needed — the pheromone system handles it.

However, agents should **deprioritize** all other needs during Exodus:
```cpp
if (clock.get_phase() == GameClock::Phase::Exodus) {
    // Override goal to "exit" — agent follows PHERO_EXIT channel
    agent.want = FacilityType::Exit; // Add Exit to FacilityType enum
}
```

### 3. Gate Exit (Agent Removal)

When an agent reaches a gate tile during Exodus:
- Agent entity is removed from simulation
- Increment `agents_exited` counter
- No service time — instant removal

```cpp
// Add to GameState
int agents_exited = 0;
```

### 4. Carryover Agents

Agents still in the park when Dead Hours begin (3am):
- They stay in the park (entities persist)
- Mark them as "carryover":

```cpp
struct CarryoverAgent : BaseComponent {
    // Tag component — no data needed
};
```

- Carryover agents resume normal behavior when Day phase starts
- **Penalty metric**: track carryover count in GameState

```cpp
// Add to GameState
int carryover_count = 0; // reset each day
```

The carryover count is displayed in the game over stats and serves as a pressure indicator — too many carryovers means the player's gate placement is insufficient.

### 5. Dead Hours Behavior

During Dead Hours (3am - 10am):
- No new agents spawn
- Carryover agents continue normal behavior (they have needs)
- Player has full building access
- Clock continues ticking normally

### 6. Exit Pheromone Flood Fill

Efficient gradient creation:

```cpp
void flood_exit_pheromone(Grid& grid) {
    // BFS from all gate tiles
    std::queue<std::pair<int,int>> frontier;

    // Seed with gate tiles
    for (int x = 0; x < Grid::SIZE; x++) {
        for (int z = 0; z < Grid::SIZE; z++) {
            if (grid.at(x,z).type == TileType::Gate) {
                grid.at(x,z).pheromone[Tile::PHERO_EXIT] = 255;
                frontier.push({x, z});
            }
        }
    }

    // BFS: each step reduces pheromone by distance
    while (!frontier.empty()) {
        auto [x, z] = frontier.front();
        frontier.pop();
        uint8_t current = grid.at(x,z).pheromone[Tile::PHERO_EXIT];
        if (current <= 5) continue; // too weak to propagate

        for each neighbor (nx, nz):
            if walkable and neighbor.pheromone[EXIT] < current - 5:
                neighbor.pheromone[EXIT] = current - 5;
                frontier.push({nx, nz});
    }
}
```

Run this once at Exodus start, then let normal decay handle the rest. Gates keep refreshing their value each frame.

### 7. FacilityType Update

Add `Exit` to the enum:

```cpp
enum class FacilityType { Bathroom, Food, Stage, Exit };
```

### 8. E2E Commands

| Command | Args | Description |
|---------|------|-------------|
| `assert_agents_exited` | `OP VALUE` | Assert agents_exited count |
| `assert_carryover_count` | `OP VALUE` | Assert carryover count |

**`tests/e2e/15_exodus.e2e`**:
```
# Test exodus behavior
reset_game
wait 5

# Spawn agents inside
draw_path_rect 1 24 50 28
spawn_agents 25 26 20 stage
wait 60

# Jump to exodus
set_time 0 0
wait 300

# Agents should be heading toward gates
screenshot 15_exodus_flow
assert_agents_exited gt 0

# Jump to dead hours
set_time 3 0
wait 30

# Any remaining agents are carryover
screenshot 15_carryover
```

**`tests/e2e/15_dead_hours.e2e`**:
```
# Test dead hours - no spawning
reset_game
wait 5

set_time 4 0
wait 5
clear_agents
wait 5

# No agents should spawn during dead hours
wait 120
assert_agent_count eq 0
screenshot 15_dead_hours_empty
```

---

## Existing Code to Leverage

- Pheromone system from Phase 14
- Gate tiles from Phase 06
- GameClock phases from Phase 11
- Agent pathfinding from Phase 03/14

---

## Acceptance Criteria

- [ ] Gates emit strong exit pheromone during Exodus
- [ ] Agents follow exit pheromone gradient toward gates
- [ ] Agents removed from simulation when reaching gate during Exodus
- [ ] Agents still in park at Dead Hours become carryover (tagged)
- [ ] Carryover count tracked in GameState
- [ ] No new agents spawn during Dead Hours
- [ ] Carryover agents resume normal behavior at Day start
- [ ] Exodus creates visible rush/chokepoint at gates

## Out of Scope

- Carryover penalty affecting gameplay (scoring)
- Multiple exit strategies
- Panic behavior during exodus

---

## Demo 3 Complete Checklist

At this point, Demo 3 ("The Schedule") should be fully playable:

- [x] Phase 11: Time clock with 4 phases, pause
- [x] Phase 12: Artist schedule, spawn rate ramp, stage glow
- [x] Phase 13: Timeline UI in sidebar
- [x] Phase 14: Pheromone trail pathfinding
- [x] Phase 15: Exodus, exit pheromone, carryover
