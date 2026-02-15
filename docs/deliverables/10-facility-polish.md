# Phase 10: Facility Polish (Demo 2 Complete)

**Demo 2E** | Prerequisite: Phase 09 (game over works)

## Goal

Polish facility behavior: proper tile footprints, smarter facility search, tuning. Make facilities feel like real bottlenecks that create organic crowd pressure.

**Done when:** Facilities feel like real bottlenecks that create organic crowd pressure.

---

## Deliverables

### 1. Proper Tile Footprints

Facilities mark multiple tiles in the grid:
- **Bathroom**: 2x2 tiles → `TileType::Bathroom`
- **Food**: 2x2 tiles → `TileType::Food`
- **Stage**: 4x4 tiles → `TileType::Stage`

When placing a facility, mark ALL tiles in its footprint. Store the "anchor" (top-left) grid position for reference.

```cpp
struct FacilityFootprint {
    int anchor_x, anchor_z; // top-left corner
    int width, height;      // in tiles
    FacilityType type;
};

// Sizes
constexpr int STAGE_SIZE = 4;    // 4x4
constexpr int BATHROOM_SIZE = 2; // 2x2
constexpr int FOOD_SIZE = 2;     // 2x2
constexpr int GATE_WIDTH = 2;    // 2x1
constexpr int GATE_HEIGHT = 1;
```

### 2. Smarter Facility Search

When agent needs a facility and the nearest is full:
1. Find closest facility of same type (Manhattan distance)
2. If full (density-based), try 2nd closest
3. If full, try 3rd closest
4. If all full:
   - **Bathroom** (urgent): keep searching, wander toward any bathroom
   - **Food** (non-urgent): give up, return to stage or wander

```cpp
// Return sorted list of facilities by distance
std::vector<std::pair<int,int>> find_facilities_by_type_sorted(
    const Grid& grid, FacilityType type, int from_x, int from_z);
```

### 3. Facility Rendering

Each facility type gets a distinct visual:
- **Stage** (4x4): warm yellow `#FFD93D`, slightly elevated look
- **Bathroom** (2x2): cyan `#7ECFC0`
- **Food** (2x2): coral `#F4A4A4`

Rendered as colored isometric tile blocks matching their footprint.

### 4. Absorption Tuning

Fine-tune service parameters:
- Service time: 1 second (confirmed)
- Facility capacity threshold: test with different values
- Agent reappearance: adjacent tile, biased toward paths

### 5. E2E Tests

**`tests/e2e/10_facility_search.e2e`**:
```
# Test facility overflow behavior
reset_game
wait 5

# Draw paths
draw_path_rect 1 24 50 28
wait 5

# Spawn many agents wanting bathroom
spawn_agents 1 26 30 bathroom
wait 120

# Agents should distribute across facilities
screenshot 10_facility_distribution
```

**`tests/e2e/10_footprints.e2e`**:
```
# Test facility tile footprints
reset_game
wait 5

# Verify stage is 4x4
assert_tile_type 30 25 stage
assert_tile_type 33 28 stage
assert_tile_type 34 25 grass

# Verify bathroom is 2x2
assert_tile_type 20 20 bathroom
assert_tile_type 21 21 bathroom
assert_tile_type 22 20 grass

screenshot 10_footprints
```

---

## Acceptance Criteria

- [ ] Facilities occupy correct tile footprint (4x4 stage, 2x2 bathroom/food)
- [ ] Agent tries 2-3 closest facilities when first is full
- [ ] Urgent need (bathroom) keeps searching, non-urgent (food) gives up
- [ ] Facility tiles render with distinct colors
- [ ] Service time of 1 sec works correctly with absorption
- [ ] Facilities create natural bottlenecks and crowd pressure

## Out of Scope

- Facility placement by player (Phase 16)
- Facility movement/pickup (Phase 16)
- Progression system (Phase 17)

---

## Demo 2 Complete Checklist

At this point, Demo 2 ("The Crush") should be fully playable:

- [x] Phase 06: Fence & gate, agents enter through gate
- [x] Phase 07: Density tracking, crush damage, deaths
- [x] Phase 08: Density overlay, death particles
- [x] Phase 09: Game over at 10 deaths, restart
- [x] Phase 10: Polished facilities, bottleneck behavior
