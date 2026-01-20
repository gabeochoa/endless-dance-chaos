# Phase 06: Fence & Gate System

## Goal
Implement fences that block movement and gates that allow entry/exit. Game starts with perimeter fence + 1 gate.

## Prerequisites
- Phase 05 complete (basic game loop with time)

## Deliverables

### 1. Fence Component
```cpp
struct Fence : BaseComponent {
    int grid_x = 0;
    int grid_z = 0;
};
```

### 2. Gate Component
```cpp
struct Gate : BaseComponent {
    int grid_x = 0;
    int grid_z = 0;
    bool is_entry = true;   // Can agents enter here?
    bool is_exit = true;    // Can agents exit here?
};
```

### 3. Initial Map Setup
At game start, create:
- Perimeter fence around 50×50 play area
- One gate (2×1 tiles) on one side
- One stage pre-placed inside

```cpp
void create_perimeter_fence() {
    for (int x = -25; x <= 25; x++) {
        make_fence(x, -25);  // Bottom edge
        make_fence(x, 25);   // Top edge
    }
    for (int z = -24; z < 25; z++) {
        make_fence(-25, z);  // Left edge
        make_fence(25, z);   // Right edge
    }
    // Gate at bottom center
    // (remove 2 fence tiles and add gate)
}
```

### 4. Movement Blocking
Modify agent movement to:
- Check if destination tile has fence
- If fence, cannot move there
- Gates allow movement through

### 5. Spawn Location
Agents spawn OUTSIDE the fence, walk to gate to enter.

## Existing Code to Use

- `PathTile` — fences are similar but block instead of allow
- Agent movement in `update_systems.cpp`
- `make_*` functions in `entity_makers.cpp`

## Afterhours to Use
- Collision system could help, but simple tile check is enough

## Implementation Steps

1. Add `Fence` and `Gate` components
2. Create `make_fence()` and `make_gate()` entity makers
3. Create `create_perimeter_fence()` in entity_makers
4. Modify agent steering to avoid fence tiles
5. Update spawn location to be outside fence
6. Render fence as simple colored tiles

## Acceptance Criteria

- [ ] Perimeter fence visible around map
- [ ] Agents cannot walk through fence tiles
- [ ] Gate visible as opening in fence
- [ ] Agents can walk through gate
- [ ] Agents spawn outside fence and enter through gate
- [ ] Cannot delete last gate (prevent soft-lock)

## Testing

### E2E Test Script: `test_fence_gate.e2e`

```
# Test: Fence and gate
reset_game
wait 10

screenshot fence_perimeter

# Agents should enter through gate
wait 300

screenshot agents_entered
get_agent_count
```

Run: `./output/dance.exe --test-mode --test-script="tests/e2e_scripts/test_fence_gate.e2e"`

### Manual Testing

1. Run game, verify fence visible around perimeter
2. Watch agents spawn outside and enter through gate
3. Verify agents don't pass through fence

## Out of Scope
- Fence building tool (Phase 08)
- Multiple gates
- Gate capacity/flow limits

