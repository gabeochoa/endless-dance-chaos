# Phase 01: Grid, Rendering & E2E Testing Framework

**Demo 1A** | Prerequisite: Game compiles and runs

## Goal

Create the 50x50 tile grid as a singleton ECS component, render it isometrically, hook up existing camera controls, set up the E2E testing framework, and load the Fredoka font.

**Done when:** You see a green 50x50 field you can pan, rotate, and zoom. E2E tests can spawn agents and take screenshots.

---

## Deliverables

### 1. Tile & Grid Data Structures

```cpp
// In components.h (new)

enum class TileType { Grass, Path, Fence, Gate, Stage, Bathroom, Food };

struct Tile {
    TileType type = TileType::Grass;
    int agent_count = 0;
    // Future phases will add: pheromone channels, facility_id, etc.
};

struct Grid : BaseComponent {
    static constexpr int SIZE = 50;
    static constexpr int TOTAL = SIZE * SIZE; // 2500
    std::array<Tile, TOTAL> tiles{};

    // 0-indexed: (0,0) to (49,49)
    int index(int x, int z) const { return z * SIZE + x; }
    bool in_bounds(int x, int z) const {
        return x >= 0 && x < SIZE && z >= 0 && z < SIZE;
    }
    Tile& at(int x, int z) { return tiles[index(x, z)]; }
    const Tile& at(int x, int z) const { return tiles[index(x, z)]; }

    // Convert world-space float position to grid coords
    // World uses TILESIZE=1.0f, grid is 0-indexed
    static std::pair<int,int> world_to_grid(float wx, float wz);
    static std::pair<float,float> grid_to_world(int gx, int gz);
};
```

- Grid is a `BaseComponent` on the sophie entity (singleton, accessed via `EntityHelper::get_singleton_cmp<Grid>()`)
- Coordinate system: 0-indexed `(0,0)` to `(49,49)`
- World space: keep `TILESIZE = 1.0f`. Rendering multiplies by 32 for pixel size.
- All tiles start as `Grass`

### 2. Grid Rendering System

Create `RenderGridSystem`:
- Iterate all 2500 tiles
- For each tile, compute isometric screen position from grid coords
- Draw as filled iso-diamond (or rectangle projected into iso space)
- Color by `TileType` using day palette initially:
  - Grass: `#98D4A8`
  - Path: `#E8DDD4`
- Use existing `IsometricCamera` from `camera.h` for world-to-screen projection

### 3. Camera Hookup

Verify existing camera controls work with the new grid:
- WASD / arrow keys: pan
- Q/E: rotate 90 degrees (snap)
- Scroll wheel: zoom (3 levels: close ~25 tiles, medium ~40, far ~60)

### 4. Font Loading

Load Fredoka font at game init:
- Copy `Fredoka-VariableFont_wdth,wght.ttf` from `~/p/wm_afterhours2/resources/fonts/` to `resources/fonts/`
- Load via raylib `LoadFontEx()` at startup
- Register with afterhours font manager if using UI system
- All text rendering from this point forward uses Fredoka

### 5. E2E Testing Framework

Sync the `e2e_testing` plugin from wordproc's afterhours vendor (or verify it's already present).

Create `src/testing/e2e_commands.h` with these custom command handlers:

| Command | Args | Description |
|---------|------|-------------|
| `spawn_agent` | `X Z TYPE` | Create agent at grid pos, TYPE = stage/bathroom/food |
| `spawn_agents` | `X Z COUNT TYPE` | Create COUNT agents at grid pos |
| `clear_agents` | (none) | Remove all agent entities |
| `clear_map` | (none) | Remove all agents + facilities, reset grid to grass |
| `reset_game` | (none) | Full game reset (clear everything, reset state) |
| `place_facility` | `TYPE X Z` | Place facility at grid pos |
| `get_agent_count` | (none) | Log current agent count |
| `get_density` | `X Z` | Log agent_count at tile (X, Z) |
| `assert_agent_count` | `OP VALUE` | Assert agent count (OP = eq/gt/lt/gte/lte) |
| `assert_density` | `X Z OP VALUE` | Assert density at tile |
| `assert_tile_type` | `X Z TYPE` | Assert tile type at position |

Each handler is a `System<testing::PendingE2ECommand>`. Use `cmd.consume()` on success, `cmd.fail(msg)` on assertion failure.

The `assert_*` commands follow the afterhours pattern: call `cmd.fail()` with a descriptive message if the assertion doesn't hold, otherwise `cmd.consume()`.

Create `src/testing/e2e_runner.h` to wire up initialization.

Update `main.cpp`:
- Add `--test-mode` and `--test-script` CLI flags
- When in test mode, register E2E handlers and load script

### 6. Initial Test Scripts

Create `tests/e2e/`:

**`tests/e2e/01_grid_exists.e2e`**:
```
# Verify grid renders
wait 5
screenshot 01_grid_default
assert_tile_type 0 0 grass
assert_tile_type 25 25 grass
```

**`tests/e2e/01_camera.e2e`**:
```
# Verify camera controls
wait 5
screenshot 01_cam_default
key E
wait 5
screenshot 01_cam_rotated
key Q
wait 5
screenshot 01_cam_back
```

### 7. Constants to Add to `game.h`

```cpp
constexpr int MAP_SIZE = 50;
constexpr int TILE_RENDER_SIZE = 32; // pixels at 1x zoom
```

---

## Existing Code to Leverage

- `camera.h` — `IsometricCamera` with rotation, pan, zoom
- `rl.h` — raylib wrapper
- `main.cpp` — CLI argument parsing with `argh`
- `game.h` — window dimensions, TILESIZE constant
- `vendor/afterhours/src/plugins/e2e_testing/` — full E2E framework

## What to Delete

- All references to `PathTile` component for grid purposes (it will be replaced by `Grid::Tile`)
- `DemandHeatmap` component (replaced by density on Grid)

---

## Acceptance Criteria

- [ ] 50x50 grid of grass tiles renders isometrically
- [ ] Camera pan (WASD), rotate (Q/E), zoom (scroll) all work
- [ ] Fredoka font loads and renders text
- [ ] `--test-mode --test-script=path.e2e` runs E2E scripts
- [ ] `spawn_agent` E2E command creates an agent entity
- [ ] `assert_tile_type` E2E command validates tile state
- [ ] `screenshot` command captures frame to file
- [ ] All test scripts in `tests/e2e/` pass without errors

## Out of Scope

- Path building (Phase 02)
- Agent movement (Phase 03)
- Density tracking (Phase 06 / Demo 2)
- Any UI beyond debug text
