# Endless Dance Chaos — Rewrite Plan

## Source of Truth

The `docs/roadmap.md` is the definitive design spec. When in doubt, follow the roadmap.

---

## Architecture

### Grid (52x52 tiles, 50x50 playable)

- Flat `std::array<Tile, 2704>` — cache-friendly, O(1) lookup by grid position
- 0-indexed coordinates: `(0,0)` to `(51,51)`
- Outer ring (row/col 0 and 51) is perimeter fence
- Playable area: `(1,1)` to `(50,50)`
- Each tile has a `TileType`: Grass, Path, Fence, Gate, Stage, Bathroom, Food
- Each tile tracks `agent_count` (density — the number that drives everything)
- Per-tile pheromone channels: 4x `uint8_t` (Bathroom, Food, Stage, Exit), range 0-255
- Grid is a singleton ECS component on the sophie entity (`EntityHelper::get_singleton_cmp<Grid>()`)
- World space uses `TILESIZE = 1.0f`; rendering multiplies by 32 for pixel size
- Tile size: 32x32 pixels at 1x zoom (1 tile = 2m x 2m real-world)

### Agents

- Position (float x, float z in world space)
- Current tile (int, derived from position — O(1) grid lookup)
- HP (1.0 — takes 0.2/sec crush damage at >90% density)
- Current goal (target facility type + target tile)
- Speed: 0.5 tiles/sec on paths, 0.25 tiles/sec on grass, linear slowdown in dangerous density
- Timer-based needs: bathroom and food timers start on spawn, trigger when threshold reached
- Urgency-based goal priority: Bathroom > Food > Stage > Exit
- Rendered as 8x8 raylib-primitive "person" shapes (single color initially)

### Pathfinding

- **Demo 1-2**: Greedy neighbor (look at 4 adjacent tiles, pick closest to goal, prefer paths)
- **Demo 3+**: Pheromone trails — agents leaving facilities deposit backward trails (uint8_t, 4 channels)
- Pheromone decay: approximately 60-second lifespan via periodic decrement
- Fallback: greedy neighbor when no pheromone exists

### Time — 24-Hour Clock

- 12 real minutes = 24 game hours
- 4 phases (Day / Night / Exodus / Dead Hours), 3 real minutes each
- Artist schedule drives spawn waves (sliding window generation)
- Display as 24-hour format (14:34)
- GameSpeed enum: Paused, OneX, TwoX, FourX (only Paused/OneX for MVP)
- Float accumulation: `game_time_minutes += (dt / SECONDS_PER_GAME_MINUTE) * speed_multiplier()`

### Density & Crush

- Max 40 agents per tile (100% density)
- Thresholds: 50% warning, 75% dangerous (movement slows linearly), 90% critical (crush damage)
- Crush damage: 0.2 HP/sec in critical zone (~5 seconds to death)
- Damage applied per-tile (iterate critical tiles, damage all agents on each)
- 10 deaths = Game Over

### Facilities

- Serviced agents are hidden/absorbed (entity hidden from world), reappear after 1 sec service time
- Capacity is density-based (agent_count on facility tiles)
- Stage watching zone: semi-circle, 8-10 tile radius, front = edge facing paths

### Exodus

- Pheromone-driven: gates emit strong exit pheromone during Exodus phase
- BFS flood fill from gate tiles creates gradient for agents to follow
- No explicit agent state change — pheromone system handles routing
- Carryover agents (stuck at Dead Hours) tagged and tracked as penalty metric

---

## What We Keep

**Keep as-is:**
- `makefile` — build system works
- `rl.h` — raylib wrapper + afterhours integration
- `std_include.h` — standard includes
- `log.h` + `log/` — logging system
- `engine/random_engine.*` — RNG
- `camera.h` — isometric camera (works great)
- `mcp_integration.h` + `mcp_systems.cpp` — MCP debugging
- `docs/` — design docs are the source of truth
- `.cursor`, `.vscode`, `.clang-format`, `.gitignore`

**Keep but modify:**
- `main.cpp` — simplify, add E2E test mode, time system init
- `input_mapping.h` — fix key collisions, add L/R bumper cycling
- `game.h` — add density constants, map size, time constants
- `systems.h` — same pattern, different systems
- `vec_util.h` — fix `remove_all_matching` bug, keep the rest

**Rewrite from scratch:**
- `components.h` — new Grid, Tile, Agent, AgentHealth, AgentNeeds, GameClock, ArtistSchedule, etc.
- `entity_makers.cpp/h` — new makers for the grid-based world
- `update_systems.cpp` — all new systems (density, crush, pheromone, time, spawning)
- `render_systems.cpp` — rewrite rendering for grid + density overlay

**Delete (replaced by new systems):**
- `PathTile` / `PathSignpost` → replaced by Grid tiles
- `HasStress` → replaced by density/crush
- `DemandHeatmap` → replaced by density on Grid
- `AgentSteering` → replaced by greedy neighbor / pheromone
- `InsideFacility` → replaced by `BeingServiced`
- `LingeringAtStage` → replaced by `WatchingStage`
- `FestivalProgress` → replaced by `FacilitySlots`
- `docs/phases/` → replaced by `docs/deliverables/` numbered files

---

## Deliverables

4 shippable demos, each fully playable. 20 phases total.

Each phase file lives in `docs/deliverables/NN-description.md`.

---

## Demo 1: "Crowd on a Grid"

**Publisher sees:** Draw paths, watch a crowd form around a stage. Simple but the foundation is clearly there.

| Phase | File | Name |
|-------|------|------|
| 01 | `01-grid-rendering-and-e2e.md` | Grid, Rendering & E2E Testing Framework |
| 02 | `02-path-building.md` | Path Building |
| 03 | `03-agent-spawning-and-movement.md` | Agent Spawning & Movement |
| 04 | `04-facilities-and-needs.md` | Facilities & Needs |
| 05 | `05-integration-and-playtest.md` | Integration & Playtest |

---

## Demo 2: "The Crush"

**Publisher sees:** Real stakes. Crowds kill people if you don't manage flow. Game over when 10 die.

| Phase | File | Name |
|-------|------|------|
| 06 | `06-fence-and-gate.md` | Fence & Gate |
| 07 | `07-density-and-crush-damage.md` | Density & Crush Damage |
| 08 | `08-density-visualization.md` | Density Visualization |
| 09 | `09-game-over.md` | Game Over |
| 10 | `10-facility-polish.md` | Facility Polish |

---

## Demo 3: "The Schedule"

**Publisher sees:** The tower defense loop — see what's coming, prepare your layout, survive the wave, repeat.

| Phase | File | Name |
|-------|------|------|
| 11 | `11-time-clock.md` | Time Clock |
| 12 | `12-artist-schedule.md` | Artist Schedule |
| 13 | `13-timeline-ui.md` | Timeline UI |
| 14 | `14-pheromone-trails.md` | Pheromone Trails |
| 15 | `15-exodus-and-carryover.md` | Exodus & Carryover |

---

## Demo 4: "MVP"

**Publisher sees:** The finished game. Full UI, all tools, progression, visual polish.

| Phase | File | Name |
|-------|------|------|
| 16 | `16-full-build-tools.md` | Full Build Tools |
| 17 | `17-progression-system.md` | Progression System |
| 18 | `18-top-bar-and-build-bar-ui.md` | Top Bar & Build Bar UI |
| 19 | `19-minimap.md` | Minimap |
| 20 | `20-polish-pass.md` | Polish Pass |

---

## Summary

| Demo | Name | Phases | Core Addition |
|------|------|--------|---------------|
| 1 | Crowd on a Grid | 01-05 | Grid, paths, agents, facilities |
| 2 | The Crush | 06-10 | Fence/gate, density, crush deaths, game over |
| 3 | The Schedule | 11-15 | Time, artists, timeline, pheromones, exodus |
| 4 | MVP | 16-20 | Full UI, all tools, progression, polish |

**Total: 20 phases across 4 demos**

Each demo is fully playable and shows clear progress toward the MVP.

---

## E2E Testing

Every phase includes E2E tests using the afterhours `e2e_testing` plugin.

**Built-in commands** (from afterhours):
- `type`, `key`, `click`, `wait`, `screenshot`
- `expect_text` / `expect_no_text` — text assertions
- `assert_no_overflow` — UI overflow check

**Custom game commands** (added incrementally per phase):
- `spawn_agent`, `spawn_agents`, `clear_agents`, `clear_map`, `reset_game`
- `set_tile`, `draw_path_rect`, `place_facility`, `place_building`, `demolish_at`
- `set_time`, `set_speed`, `set_spawn_rate`, `set_max_attendees`
- `select_tool`, `toggle_overlay`, `trigger_game_over`
- `assert_*` commands for automated validation (tile type, density, agent count, death count, phase, etc.)
- `get_*` commands for console logging (density, agent count, death count, etc.)

Run: `./output/dance.exe --test-mode --test-script="tests/e2e/NN_test_name.e2e"`

---

## Tuning Parameters

| Parameter | Value | Description |
|-----------|-------|-------------|
| `MAP_SIZE` | 52x52 (50x50 playable) | Tiles (including fence perimeter) |
| `TILE_RENDER_SIZE` | 32px | Visual pixels per tile at 1x zoom |
| `TILESIZE` | 1.0f | World-space tile size |
| `MAX_AGENTS_PER_TILE` | 40 | 100% density |
| `DENSITY_WARNING` | 50% (20) | Yellow overlay |
| `DENSITY_DANGEROUS` | 75% (30) | Movement slows linearly |
| `DENSITY_CRITICAL` | 90% (36) | Crush damage begins |
| `AGENT_HP` | 1.0 | Dies in ~5 sec at critical |
| `CRUSH_DAMAGE_RATE` | 0.2/sec | Flat damage in critical zone |
| `MAX_DEATHS` | 10 | Game over threshold |
| `SPEED_PATH` | 0.5 tiles/sec | Base walking speed |
| `SPEED_GRASS` | 0.25 tiles/sec | Half speed off paths |
| `SERVICE_TIME` | 1 sec | Bathroom/food service duration |
| `PATIENCE_TIMER` | 60-120 sec | Before agent gives up |
| `WATCH_DURATION` | 30-120 sec | Random time watching stage |
| `SECONDS_PER_GAME_MINUTE` | 0.5 sec | 12 real min = 24 game hours |
| `PHASE_LENGTH` | 3 min real | Per time phase |
| `DAY_CYCLE` | 12 min real | Full 24-hour cycle |
| `PHEROMONE_CHANNELS` | 4 x uint8_t | Bathroom, Food, Stage, Exit |
| `PHEROMONE_MAX` | 255 (uint8_t) | Fresh trail strength |
| `PHEROMONE_LIFESPAN` | ~60 sec | Time to full decay |
| `SEPARATION_RADIUS` | 0.25 tiles | Push-apart distance |

---

## Key Design Decisions

These were made during planning and should NOT be revisited by the intern:

1. **Grid**: Singleton ECS component, 0-indexed, 52x52 (including fence ring)
2. **Pheromones**: 4x `uint8_t` per tile (not floats). Memory-dense.
3. **Pathfinding**: Greedy neighbor (Demos 1-2), pheromone trails (Demo 3+)
4. **Agents**: Raylib primitives (8x8 person shape), single color initially
5. **Facility service**: Agent hidden/absorbed during service, reappears after 1 sec
6. **Facility capacity**: Density-based (agent_count on facility tiles)
7. **Stage watching**: Semi-circle zone, front = edge facing paths
8. **Pause**: Global `GameSpeed` enum (Paused/1x/2x/4x), systems check before running
9. **Exodus**: Pheromone-driven (gates emit exit pheromone), no explicit state change
10. **Carryover**: Tagged agents, tracked as penalty metric
11. **Day/night**: Smoothstep interpolation over 1 game-hour
12. **Minimap**: Separate RenderTexture, 150x150px
13. **Font**: Fredoka, loaded in Phase 01
14. **Mouse-only building** for Demos 1-3, controller in Demo 4
15. **Spawn point**: Fixed at left edge center `(0, 26)`, outside fence
16. **Pre-placed stage**: Grid `(30, 25)`, fixed offset from center
17. **Facility move**: Try pickup-and-move first, fall back to demolish+rebuild
18. **Unlock notification**: Top bar toast that fades after 2-3 seconds
