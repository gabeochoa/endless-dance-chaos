# Refactor 02: Collapse Agent Sub-Components into `Agent`

## Problem

Each agent entity carries 5–7 separate components: `Agent`, `AgentNeeds`, `AgentHealth`, `WatchingStage`, `BeingServiced`, `PheromoneDepositor`, `CarryoverAgent`. Nearly every system that touches agents checks for 2–3 of these via `e.is_missing<T>()` / `e.get<T>()`.

### Current patterns

```cpp
// AgentMovementSystem
void for_each_with(Entity& e, Agent& agent, Transform& tf, float dt) override {
    if (!e.is_missing<BeingServiced>()) return;
    // ...
    if (!e.is_missing<WatchingStage>()) return;
    // ...
    if (!e.is_missing<AgentHealth>()) { /* check hp */ }
}
```

`AgentNeeds` and `AgentHealth` are added to every agent at creation — they're never optional. `WatchingStage` and `BeingServiced` are effectively state-machine transitions with a timer attached.

### Component usage analysis

| Component | Optional? | Fields | Used by |
|-----------|-----------|--------|---------|
| `Agent` | No | want, target, speed, stuck_timer, color_idx, flee | Movement, Goals, Rendering |
| `AgentNeeds` | No | bathroom/food timers and flags | NeedTick, Goals, FacilityService |
| `AgentHealth` | No | hp (float) | CrushDamage, Goals (medical), Rendering |
| `WatchingStage` | Yes (state) | watch_timer, watch_duration | StageWatching, Movement (skip), Goals (skip) |
| `BeingServiced` | Yes (state) | facility coords, type, time_remaining | FacilityService, Movement (skip), Goals (skip), Density (skip) |
| `PheromoneDepositor` | Yes (state) | leaving_type, is_depositing, distance | PheromoneDeposit |
| `CarryoverAgent` | Yes (tag) | (empty) | Exodus |

## Proposed Fix

### Merge always-present components

Move `AgentNeeds` and `AgentHealth` fields directly into `Agent`:

```cpp
struct Agent : afterhours::BaseComponent {
    // Goal
    FacilityType want = FacilityType::Stage;
    int target_grid_x = -1, target_grid_z = -1;
    float speed = SPEED_PATH;
    int flee_target_x = -1, flee_target_z = -1;
    uint8_t color_idx = 0;
    float stuck_timer = 0.f;
    int last_grid_x = -1, last_grid_z = -1;

    // Health (was AgentHealth)
    float hp = 1.0f;

    // Needs (was AgentNeeds)
    float bathroom_timer = 0.f, bathroom_threshold = 0.f;
    float food_timer = 0.f, food_threshold = 0.f;
    bool needs_bathroom = false, needs_food = false;

    // ...
};
```

### Replace state components with an enum

Replace `WatchingStage`, `BeingServiced`, `PheromoneDepositor` with:

```cpp
enum class AgentState { Moving, Watching, Serviced, Depositing };

struct Agent : afterhours::BaseComponent {
    // ...existing fields...

    AgentState state = AgentState::Moving;

    // Watching state (was WatchingStage)
    float watch_timer = 0.f, watch_duration = 0.f;

    // Service state (was BeingServiced)
    int facility_grid_x = 0, facility_grid_z = 0;
    FacilityType facility_type = FacilityType::Bathroom;
    float service_time_remaining = 0.f;

    // Pheromone state (was PheromoneDepositor)
    FacilityType leaving_type = FacilityType::Bathroom;
    float deposit_distance = 0.f;

    // Carryover flag (was CarryoverAgent)
    bool is_carryover = false;
};
```

### System changes

All `e.is_missing<BeingServiced>()` becomes `agent.state != AgentState::Serviced`.
All `e.addComponent<WatchingStage>()` becomes `agent.state = AgentState::Watching; agent.watch_timer = 0;`.
Systems that query `System<Agent, AgentNeeds, Transform>` become `System<Agent, Transform>`.

## Files to change

- `src/components.h` — Merge components, add `AgentState` enum
- `src/agent_systems.cpp` — Update all agent systems
- `src/crowd_systems.cpp` — Update density, damage, death, exodus systems
- `src/entity_makers.cpp` — Simplify `make_agent`, `reset_game_state`
- `src/render_world.cpp` — Update `RenderAgentsSystem`
- `src/render_ui.cpp` — Update minimap and hover info
- `src/polish_systems.cpp` — Update bottleneck checks

## Estimated impact

- **Performance:** Eliminates ~6 `is_missing<T>()` / `get<T>()` lookups per agent per system per frame. With 200 agents and 10 systems, that's ~12,000 fewer dynamic lookups per frame.
- **Complexity:** Removes 5 component structs. All agent state is in one place. State transitions are explicit enum changes instead of add/remove component patterns.

## Risks

- Larger `Agent` struct means more memory per agent even when fields are inactive. But the fields are small (floats, ints) and agents are already few enough that this doesn't matter.
- Systems can no longer use `System<Agent, WatchingStage>` to auto-filter — they must manually check `agent.state`. This is a slight ergonomic downgrade but matches the existing pattern (most systems already check `is_missing` manually).
