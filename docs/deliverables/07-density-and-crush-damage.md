# Phase 07: Density & Crush Damage

**Demo 2B** | Prerequisite: Phase 06 (fence/gate, agents enter through gate)

## Goal

Agents have 1 HP. Critical density (>90%) causes crush damage. HP <= 0 = death. Track death count.

**Done when:** Packing 40+ agents in one tile kills them in ~5 seconds.

---

## Deliverables

### 1. Agent Health Component

```cpp
struct AgentHealth : BaseComponent {
    float hp = 1.0f;
};
```

Add to all agents on spawn.

### 2. Density Constants

Add to `game.h`:

```cpp
constexpr int MAX_AGENTS_PER_TILE = 40;
constexpr float DENSITY_WARNING = 0.50f;    // 20 agents — yellow
constexpr float DENSITY_DANGEROUS = 0.75f;  // 30 agents — movement slows
constexpr float DENSITY_CRITICAL = 0.90f;   // 36 agents — crush damage
constexpr float CRUSH_DAMAGE_RATE = 0.2f;   // HP/sec in critical zone
constexpr int MAX_DEATHS = 10;
```

### 3. Crush Damage System (Per-Tile)

Create `CrushDamageSystem`:
- Iterate tiles (not agents) — more efficient at scale
- For each tile where `agent_count > MAX_AGENTS_PER_TILE * DENSITY_CRITICAL` (36+):
  - Find all agents on that tile
  - Apply `CRUSH_DAMAGE_RATE * dt` damage to each
- For tiles at dangerous density (75-90%): apply movement speed reduction (linear)

```cpp
// Pseudocode
for each tile in grid:
    float density = tile.agent_count / (float)MAX_AGENTS_PER_TILE;
    if density >= DENSITY_CRITICAL:
        for each agent on this tile:
            agent_health.hp -= CRUSH_DAMAGE_RATE * dt;
```

### 4. Agent Death

When `AgentHealth::hp <= 0`:
- Mark agent entity for deletion (`entity.cleanup = true`)
- Increment death counter in `GameState`
- Log death with grid position: `"Agent died at (X, Z), deaths: N/10"`

```cpp
// Add to GameState
int death_count = 0;
int max_deaths = MAX_DEATHS; // 10
```

### 5. Movement Slowdown in Dangerous Zones

At 75-90% density, linearly reduce agent speed:

```cpp
float density_speed_modifier(float density_ratio) {
    if (density_ratio < DENSITY_DANGEROUS) return 1.0f;
    if (density_ratio >= DENSITY_CRITICAL) return 0.1f; // near-stop
    // Linear interpolation between dangerous and critical
    float t = (density_ratio - DENSITY_DANGEROUS) / (DENSITY_CRITICAL - DENSITY_DANGEROUS);
    return 1.0f - (t * 0.9f); // 1.0 → 0.1
}
```

### 6. E2E Commands

| Command | Args | Description |
|---------|------|-------------|
| `get_death_count` | (none) | Log current death count |
| `assert_death_count` | `OP VALUE` | Assert death count (eq/gt/lt/gte/lte) |
| `assert_agent_hp` | `X Z OP VALUE` | Assert HP of agents at tile |

**`tests/e2e/07_crush_damage.e2e`**:
```
# Test crush damage at critical density
reset_game
wait 5

# Spawn at critical density (40 agents on one tile)
spawn_agents 25 25 40 stage
wait 5

# Before deaths
assert_death_count eq 0
screenshot 07_before_deaths

# Wait ~6 seconds for deaths (1 HP / 0.2 per sec = 5 sec)
wait 360

# Deaths should have occurred
assert_death_count gt 0
screenshot 07_after_deaths
get_death_count
```

**`tests/e2e/07_no_damage_below_critical.e2e`**:
```
# Test no damage below critical density
reset_game
wait 5

# Spawn below critical (30 agents = 75% = dangerous but not critical)
spawn_agents 25 25 30 stage
wait 300

# No deaths should occur
assert_death_count eq 0
screenshot 07_no_damage
```

---

## Existing Code to Leverage

- `tile.agent_count` from Phase 05
- `GameState` singleton for death tracking
- Agent movement system for speed modification

## What to Delete

- `HasStress` component — replaced by density-based crush
- Stress-related systems and thresholds in `GameState`

---

## Acceptance Criteria

- [ ] Agents have 1.0 HP
- [ ] Critical density (>36 agents/tile) causes 0.2 HP/sec damage
- [ ] HP <= 0 = agent dies and is removed
- [ ] Death count increments correctly
- [ ] Deaths logged to console with location
- [ ] Dangerous density (75-90%) slows movement linearly
- [ ] Below 75% density: no damage, no slowdown
- [ ] E2E assertions for death count work

## Out of Scope

- Game over screen (Phase 09)
- Death particle effect (Phase 20)
- Density overlay visualization (Phase 08)
