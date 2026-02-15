# Phase 14: Pheromone Trails

**Demo 3D** | Prerequisite: Phase 12 (artists drive crowd flow)

## Goal

Agents leaving facilities deposit pheromone trails. Arriving agents follow strongest trails toward their goal type. Emergent pathfinding replaces pure greedy-neighbor.

**Done when:** Popular routes form naturally, bottlenecks emerge organically.

---

## Deliverables

### 1. Pheromone Data in Tiles

Add pheromone channels to the `Tile` struct:

```cpp
struct Tile {
    TileType type = TileType::Grass;
    int agent_count = 0;

    // 4 pheromone channels: Bathroom, Food, Stage, Exit
    // uint8_t: 0 = no pheromone, 255 = max strength (maps to 0.0-10.0)
    std::array<uint8_t, 4> pheromone = {0, 0, 0, 0};

    // Channel indices
    static constexpr int PHERO_BATHROOM = 0;
    static constexpr int PHERO_FOOD = 1;
    static constexpr int PHERO_STAGE = 2;
    static constexpr int PHERO_EXIT = 3;

    // Convert uint8_t to float strength (0.0-10.0)
    static float to_strength(uint8_t val) { return val * (10.0f / 255.0f); }
    static uint8_t from_strength(float s) {
        return static_cast<uint8_t>(std::clamp(s * 25.5f, 0.0f, 255.0f));
    }
};
```

Memory: 4 bytes per tile × 2704 tiles = ~11 KB. Cache-friendly.

### 2. Pheromone Deposit (Backward Trail)

Agents deposit pheromone AFTER leaving a facility:
- When agent exits a facility (service complete), start depositing
- On each tile the agent walks over, deposit pheromone for that facility type
- Fresh deposit: strength 10 (uint8_t value 255)
- Deposit is additive up to max (don't exceed 255)

```cpp
// Agent state for tracking pheromone deposit
struct PheromoneDepositor : BaseComponent {
    FacilityType leaving_type;
    bool is_depositing = false;
    float deposit_distance = 0.f;
    static constexpr float MAX_DEPOSIT_DISTANCE = 30.0f; // tiles
};
```

Agent stops depositing after walking ~30 tiles from the facility.

### 3. Pheromone Decay

Create `DecayPheromonesSystem`:
- Each frame, decay all pheromone values across all tiles
- Decay rate: 0.17 strength/sec → ~0.67 uint8_t/sec at 25.5 scale
- Implementation: every N frames, subtract 1 from all non-zero pheromone values

```cpp
// Efficient: run every 6 real-time frames (~0.1 sec)
// Subtract 1 from each non-zero pheromone every ~1.5 sec
// This approximates 0.17/sec decay (10 → 0 in ~60 seconds)
static int decay_frame_counter = 0;
decay_frame_counter++;
if (decay_frame_counter % 90 == 0) { // every ~1.5 sec at 60fps
    for (auto& tile : grid.tiles) {
        for (auto& p : tile.pheromone) {
            if (p > 0) p--;
        }
    }
}
```

### 4. Pheromone Following

Update agent pathfinding to prefer pheromone trails:

When choosing next tile (greedy neighbor), add pheromone as a factor:

```cpp
float score_tile(int x, int z, int goal_x, int goal_z,
                 FacilityType want, const Grid& grid) {
    float distance_score = manhattan_distance(x, z, goal_x, goal_z);
    float pheromone_score = Tile::to_strength(
        grid.at(x, z).pheromone[facility_to_channel(want)]);

    // Lower score = better. Pheromone reduces score.
    return distance_score - (pheromone_score * 2.0f);
}
```

Fallback: if no pheromone exists, pure greedy neighbor (beeline toward goal).

### 5. Async Decay (Performance)

Pheromone decay should not block the frame:
- Process decay in batches across frames if needed
- At 2704 tiles × 4 channels = ~10K operations — fast enough to do in one frame
- Profile and batch only if it becomes a problem

### 6. Debug Overlay (Optional)

When TAB overlay is active, add a pheromone visualization mode:
- Show arrows or intensity per tile for selected facility type
- Toggle facility type filter with number keys (1=bathroom, 2=food, 3=stage, 4=exit)
- Color intensity: brighter = stronger pheromone

### 7. E2E Commands

| Command | Args | Description |
|---------|------|-------------|
| `set_pheromone` | `X Z CHANNEL VALUE` | Set pheromone at tile (0-255) |
| `assert_pheromone` | `X Z CHANNEL OP VALUE` | Assert pheromone value |
| `clear_pheromones` | (none) | Reset all pheromones to 0 |

**`tests/e2e/14_pheromone_deposit.e2e`**:
```
# Test pheromone deposit after facility visit
reset_game
wait 5

# Set up paths to bathroom
draw_path_rect 1 24 25 26
wait 5

# Spawn agent wanting bathroom
spawn_agent 20 20 bathroom
wait 60

# After service, agent should deposit pheromone on way out
# Check pheromone on path tiles
assert_pheromone 20 25 0 gt 0
screenshot 14_pheromone_deposited
```

**`tests/e2e/14_pheromone_decay.e2e`**:
```
# Test pheromone decay
reset_game
wait 5

# Manually set strong pheromone
set_pheromone 25 25 0 255
wait 5
assert_pheromone 25 25 0 eq 255

# Wait for decay (~60 sec at normal speed)
wait 3600

# Pheromone should have decayed significantly
assert_pheromone 25 25 0 lt 100
screenshot 14_pheromone_decayed
```

---

## Existing Code to Leverage

- Grid tile array
- Greedy neighbor pathfinding (Phase 03)
- `BeingServiced` component for detecting facility exit

---

## Acceptance Criteria

- [ ] Agents leaving facilities deposit pheromone trails (backward direction)
- [ ] Pheromone uses uint8_t (0-255) per channel, 4 channels per tile
- [ ] Arriving agents prefer tiles with stronger pheromone for their goal type
- [ ] Pheromone decays over ~60 seconds
- [ ] Popular routes get reinforced naturally (more agents = stronger trail)
- [ ] No pheromone → fallback to greedy neighbor (beeline)
- [ ] Performance: decay system handles 2704 tiles without frame impact

## Out of Scope

- Exodus pheromone (Phase 15)
- Full debug overlay visualization (nice-to-have)
