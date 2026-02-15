# Phase 08: Density Visualization

**Demo 2C** | Prerequisite: Phase 07 (density tracking + crush damage)

## Goal

TAB toggles a heat map overlay showing crowd density per tile. Gradient from transparent → yellow → orange → red → black. Isometric-aligned.

**Done when:** You can see danger building visually before deaths happen.

---

## Deliverables

### 1. Overlay Toggle

TAB key toggles `GameState::show_data_layer` (already exists).

### 2. Isometric Overlay Rendering

Create `RenderDensityOverlaySystem`:
- Only runs when `show_data_layer == true`
- For each tile with `agent_count > 0`:
  - Calculate density ratio: `agent_count / (float)MAX_AGENTS_PER_TILE`
  - Get gradient color from ratio
  - Draw semi-transparent isometric diamond matching the tile's grid position
- Must align with camera rotation (use same iso projection as grid tiles)

### 3. Gradient Colors

```cpp
Color get_density_color(float density_ratio) {
    if (density_ratio < 0.50f) {
        // Transparent → Yellow (0% - 50%)
        float t = density_ratio / 0.50f;
        return Color{255, 255, 0, (uint8_t)(t * 180)};
    } else if (density_ratio < 0.75f) {
        // Yellow → Orange (50% - 75%)
        float t = (density_ratio - 0.50f) / 0.25f;
        return Color{255, (uint8_t)(255 - t * 90), 0, 180};
    } else if (density_ratio < 0.90f) {
        // Orange → Red (75% - 90%)
        float t = (density_ratio - 0.75f) / 0.15f;
        return Color{255, (uint8_t)(165 - t * 165), 0, 200};
    } else {
        // Red → Black (90% - 100%)
        float t = (density_ratio - 0.90f) / 0.10f;
        return Color{(uint8_t)(255 - t * 255), 0, 0, 220};
    }
}
```

### 4. Death Particle Burst

When an agent dies, spawn a particle burst:
- 5-8 small square particles
- Size: 2-4px
- Burst radius: 8-12px
- Duration: 0.3-0.5 sec
- Color: agent color or red/white

```cpp
struct Particle : BaseComponent {
    vec2 velocity;
    float lifetime;
    float max_lifetime;
    float size;
    Color color;
};
```

Create `SpawnDeathParticlesSystem` that triggers on agent death.
Create `UpdateParticlesSystem` to move particles and fade alpha.
Create `RenderParticlesSystem` to draw particles.

### 5. Mass Death Merge

If 5+ agents die on the same tile in the same frame:
- Merge into one bigger burst: 10-15 particles, 20-30px radius

Track deaths per tile per frame:
```cpp
// In CrushDamageSystem, accumulate deaths per tile
std::unordered_map<int, int> deaths_this_frame; // tile_index → count
```

### 6. E2E Commands

| Command | Args | Description |
|---------|------|-------------|
| `toggle_overlay` | (none) | Toggle density overlay |
| `assert_overlay` | `on/off` | Assert overlay state |

**`tests/e2e/08_density_overlay.e2e`**:
```
# Test density overlay
reset_game
wait 5

# Toggle overlay on
key TAB
wait 5
screenshot 08_overlay_empty

# Spawn agents at different densities
spawn_agents 20 25 20 stage
spawn_agents 25 25 30 stage
spawn_agents 30 25 38 stage
wait 30

screenshot 08_overlay_gradient

# Toggle off
key TAB
wait 5
screenshot 08_overlay_off
```

**`tests/e2e/08_death_particles.e2e`**:
```
# Test death particle effect
reset_game
wait 5

# Spawn critical density for deaths
spawn_agents 25 25 45 stage
wait 300

screenshot 08_death_particles
```

---

## Existing Code to Leverage

- `GameState::show_data_layer` — TAB toggle
- Tile density from Phase 07
- Isometric rendering pipeline from Phase 01

---

## Acceptance Criteria

- [ ] TAB toggles overlay on/off
- [ ] Overlay draws isometric diamonds matching tile positions
- [ ] Colors match gradient: transparent → yellow → orange → red → black
- [ ] Overlay is semi-transparent (game visible beneath)
- [ ] Updates in real-time as agents move
- [ ] Death particle burst visible when agent dies
- [ ] 5+ simultaneous deaths on one tile produce merged bigger burst
- [ ] No performance impact when overlay is off

## Out of Scope

- Colorblind patterns (post-MVP)
- Minimap density display
- Filtering by facility type
