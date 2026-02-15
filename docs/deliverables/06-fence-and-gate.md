# Phase 06: Fence & Gate

**Demo 2A** | Prerequisite: Phase 05 (Demo 1 complete)

## Goal

Auto-place perimeter fence around the playable area. Agents spawn outside, enter through a gate. Fences block all movement.

**Done when:** Agents spawn outside, funnel through gates, fence blocks movement.

---

## Deliverables

### 1. Extended Grid

The grid is now **52x52** internally:
- Outer ring (row/col 0 and 51) is the fence perimeter
- Inner 50x50 (rows/cols 1-50) is the playable area
- Update `Grid::SIZE` to 52, `Grid::TOTAL` to 2704
- Playable area: `(1,1)` to `(50,50)` in grid coords

```cpp
struct Grid : BaseComponent {
    static constexpr int SIZE = 52;
    static constexpr int TOTAL = SIZE * SIZE; // 2704
    static constexpr int PLAY_MIN = 1;
    static constexpr int PLAY_MAX = 50;
    // ...
};
```

Update all existing code that references grid coordinates to account for the +1 offset.

### 2. Perimeter Fence

At game start, set outer ring to `TileType::Fence`:
- Row 0 and row 51: all fence
- Col 0 and col 51: all fence
- Fence tiles block all agent movement (greedy neighbor skips them)

### 3. Default Gate

Place one gate on the **left edge center**: tiles `(0, 26)` and `(0, 27)` become `TileType::Gate`.

Gate behavior:
- Agents can walk through gate tiles (treated as walkable)
- Gate is a 2x1 opening in the fence

### 4. Spawn Point Update

Move spawn point to **outside the fence**: `(-1, 26)` in world space (or just spawn at the gate tile).

Actually, since agents spawn outside and walk in, spawn them at a position just outside the gate:
- Spawn position: world space equivalent of grid `(0, 26)` minus a small offset
- Agent immediately targets the gate tile, then proceeds to their goal inside

### 5. Movement Blocking

Update greedy neighbor pathfinding (Phase 03):
- `TileType::Fence` is never a valid move target
- `TileType::Gate` is walkable
- Agent cannot move to out-of-bounds tiles

### 6. Fence Rendering

Fence tiles render as a distinct color:
- Fence: `#555555` (dark gray) — solid-looking barrier
- Gate: `#4488AA` (blue-tinted) — visible opening

### 7. Gate Protection

Cannot delete the last gate (prevents soft-lock). Track gate count:

```cpp
int count_gates(const Grid& grid) {
    int count = 0;
    for (auto& tile : grid.tiles) {
        if (tile.type == TileType::Gate) count++;
    }
    return count;
}
```

### 8. E2E Commands

| Command | Args | Description |
|---------|------|-------------|
| `place_gate` | `X Z` | Place a 2x1 gate at position |
| `assert_gate_count` | `OP VALUE` | Assert gate count |
| `assert_blocked` | `X Z` | Assert tile is not walkable |

**`tests/e2e/06_fence_gate.e2e`**:
```
# Test fence blocks movement
reset_game
wait 5

# Verify fence exists
assert_tile_type 0 0 fence
assert_tile_type 51 51 fence
assert_blocked 0 0

# Verify gate exists
assert_tile_type 0 26 gate
assert_gate_count gte 1

screenshot 06_fence_perimeter

# Spawn agents - they should enter through gate
spawn_agents 0 26 10 stage
wait 120

assert_agent_near 30 25 10
screenshot 06_agents_entered
```

---

## Existing Code to Leverage

- Grid from Phase 01 (extend to 52x52)
- Greedy neighbor from Phase 03 (add fence blocking)
- Agent spawning from Phase 03 (update spawn location)

---

## Acceptance Criteria

- [ ] Perimeter fence visible around map (52x52 outer ring)
- [ ] Fence tiles block agent movement completely
- [ ] Gate visible as opening in fence (2x1, left center)
- [ ] Agents spawn outside fence, enter through gate
- [ ] Cannot delete the last gate
- [ ] Playable area is still 50x50 inside the fence

## Out of Scope

- Player placing fences/gates (Phase 16 / Demo 4A)
- Multiple gates at start
- Gate capacity/flow limits
