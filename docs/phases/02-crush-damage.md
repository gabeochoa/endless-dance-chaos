# Phase 02: Crush Damage

## Goal
Agents take damage when in critical density zones (>90%). Agents have 1 HP and die after ~5 seconds. Track death count.

## Prerequisites
- Phase 01 complete (density tracking works)

## Deliverables

### 1. Agent Health Component
Add to `components.h`:
```cpp
struct AgentHealth : BaseComponent {
    float hp = 1.0f;
    static constexpr float CRUSH_DAMAGE_RATE = 0.2f; // per second
};
```

### 2. Death Counter in GameState
Add to `GameState`:
```cpp
int death_count = 0;
static constexpr int MAX_DEATHS = 10;
```

### 3. Crush Damage System
Create `CrushDamageSystem`:
- For each agent, check tile density
- If density > 90%, apply 0.2 damage per second
- If HP <= 0, agent dies

### 4. Agent Death
When agent dies:
- Remove agent entity
- Increment `death_count`
- Log death location (for future heatmap)

## Existing Code to Use

- `HasStress` component — could repurpose or replace with health
- `PathTile::congestion_ratio()` — already calculates ratio
- Agent entity removal patterns in `update_systems.cpp`

## Afterhours to Use
- `EntityHelper::schedule_for_deletion()` for safe removal

## Implementation Steps

1. Add `AgentHealth` component to all agents on spawn
2. Create system that:
   - Finds tile agent is on
   - Checks density
   - Applies damage if critical
   - Marks for deletion if dead
3. Add death counter to GameState
4. Log deaths for debugging

## Acceptance Criteria

- [ ] Agents lose HP when in crowded tiles (>36 agents)
- [ ] Agents die after ~5 seconds in critical zone
- [ ] Death count increments correctly
- [ ] Dead agents disappear from world
- [ ] Console shows death events

## Out of Scope
- Game over screen (Phase 03)
- Death particle effect (Phase 15)
- Any UI display of deaths

