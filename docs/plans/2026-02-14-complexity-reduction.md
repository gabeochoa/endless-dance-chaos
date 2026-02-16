# Complexity Reduction Plan

24 items to reduce complexity, deduplicate code, and improve performance. Ordered from simplest to most complex.

## Trivial

| # | Area | Item | Files | Impact |
|---|------|------|-------|--------|
| 1 | Dedup | **Duplicate UI text helpers** — `RenderUISystem::draw_text/measure_text/draw_text_centered` and standalone `ui_draw_text/ui_measure_text/ui_draw_text_centered` are identical. Delete the standalone ones, use the struct's static methods everywhere. | `render_systems.cpp` | Readability |
| 2 | Cleanup | **Tool cycling modulus is wrong** — `(t + 6) % 7` and `(t + 1) % 7` should be `% 8` since there are 8 tools (including MedTent/Demolish). | `update_systems.cpp:262-270` | Bug |
| 3 | Cleanup | **`can_place_at` redundantly checks bounds** — calls `in_bounds()` then manually checks `PLAY_MIN/PLAY_MAX`. Use `in_playable()` from Grid instead. | `update_systems.cpp:226-239` | Readability |
| 4 | Perf | **`facility_to_channel` switch → constexpr array** — Replace 5-case switch with a `constexpr int channels[]` indexed by enum. | `update_systems.cpp:1022-1036` | Minor perf |
| 5 | Cleanup | **`tile_blocks_movement` → lookup table** — Replace 5-way OR with `constexpr bool BLOCKS[]` indexed by TileType. | `update_systems.cpp:397-401` | Minor perf |
| 6 | Dedup | **Minimap tile color duplicates grid tile color** — `minimap_tile_color()` and `RenderGridSystem::tile_color()` define the same color mapping. Extract a shared `tile_base_color(TileType)`. | `render_systems.cpp` | Readability |

## Easy

| # | Area | Item | Files | Impact |
|---|------|------|-------|--------|
| 7 | Dedup | **Facility placement logic duplicated 3x** — `PathBuildSystem`, E2E `place_building`, and `Grid::init_perimeter` all have identical footprint loops. Extract `Grid::place_footprint(x, z, w, h, TileType)`. | `update_systems.cpp`, `e2e_commands.h`, `components.h` | ~80 lines |
| 8 | Dedup | **Game reset duplicated between `RestartGameSystem` and E2E `reset_game`** — Nearly identical 60-line blocks. Extract `reset_game_state()` free function. | `update_systems.cpp`, `e2e_commands.h` | ~60 lines |
| 9 | Dedup | **Toast creation boilerplate** — `EntityHelper::createEntity()` + `addComponent<ToastMessage>()` + `.text = ...` + `play_toast()` appears 6+ times. Extract `spawn_toast(text, lifetime)`. | `update_systems.cpp` | ~30 lines |
| 10 | Cleanup | **`FacilityServiceSystem` facility-type matching** — The 3-way if/else checking `agent.want == Bathroom && cur_type == Bathroom` etc. could use a `FacilityType→TileType` mapping function. | `update_systems.cpp:991-1001` | Readability |
| 11 | Perf | **`count_gates` scans entire grid** — Called during demolish. Track gate count in `Grid` or `GameState` instead of scanning 2704 tiles. | `update_systems.cpp:243-249` | Perf at scale |
| 12 | Cleanup | **Exodus gate pheromone refresh scans entire grid** — The "keep gate pheromone at max" loop iterates all 2704 tiles every frame during exodus. Cache gate positions in a small vector. | `update_systems.cpp:1115-1121` | Perf |

## Medium

| # | Area | Item | Files | Impact |
|---|------|------|-------|--------|
| 13 | Perf | **`find_nearest_facility` scans entire grid + allocates** — Every agent seeking a facility triggers a full 2704-tile scan with heap allocations (`vector`, `set`). Cache facility positions in Grid on tile change. | `update_systems.cpp:742-789` | Major perf |
| 14 | Perf | **`RenderFacilityLabelsSystem` scans grid + stack-allocates 3x `bool[2704]`** — 8KB on the stack per frame, plus a full grid scan. Cache label positions on build, not per-frame. | `render_systems.cpp:447-449` | Perf + stack |
| 15 | Perf | **`random_stage_spot` scans grid + sorts every call** — Collects all StageFloor tiles, sorts by distance, every time an agent spawns or finishes service. Pre-compute sorted StageFloor list once on init. | `update_systems.cpp:641-709` | Perf |
| 16 | Dedup | **E2E commands file is 1650+ lines of boilerplate** — Each command is a separate struct with identical guard pattern (`is_consumed`, `!cmd.is(...)`, `has_args`). A command-dispatch table or macro would cut this by 60%. | `e2e_commands.h` | ~900 lines |
| 17 | Perf | **Minimap re-renders every frame** — Renders all 2704 tiles to a separate RenderTexture every frame. Dirty-flag it: only re-render when a tile type changes. | `render_systems.cpp:895-960` | Perf |
| 18 | Cleanup | **Pheromone decay is frame-rate dependent** — `frame_counter % 90` assumes 60fps. Use a time accumulator instead of frame counting. | `update_systems.cpp:1186-1191` | Correctness |
| 19 | Cleanup | **`EntityHelper::cleanup()` called mid-frame** — `RandomEventSystem` calls `EntityHelper::cleanup()` between entity queries in the same `once()`. This can invalidate iterators or cause double-cleanup. Defer cleanup to end of frame. | `update_systems.cpp:1554` | Stability |
| 20 | Perf | **`UpdateAgentGoalSystem` runs `find_nearest_facility` every frame for every agent** — Even agents already heading to the correct facility re-evaluate. Add a "goal valid" check to skip agents already pathing correctly. | `update_systems.cpp:815-878` | Major perf |
| 21 | Cleanup | **Save format has no version field** — Only a magic number, no way to handle schema changes. Add a version uint32 after the magic so future format changes don't corrupt old saves. | `save_system.h` | Robustness |

## Hard

| # | Area | Item | Files | Impact |
|---|------|------|-------|--------|
| 22 | Perf | **`UpdateTileDensitySystem` queries all agents every frame** — With 500+ agents, this is significant. Could be maintained incrementally (update count when agent moves to a new tile). | `update_systems.cpp:1204-1229` | Major perf |
| 23 | Arch | **`update_systems.cpp` is 1736 lines** — All game logic in one file. Split into files by domain: `movement_systems.cpp`, `building_systems.cpp`, `schedule_systems.cpp`, `event_systems.cpp`. | `update_systems.cpp` | Maintainability |
| 24 | Arch | **`render_systems.cpp` is 1186 lines with mixed concerns** — Grid rendering, UI, minimap, debug panel all in one file. Split into `render_world.cpp`, `render_ui.cpp`, `render_debug.cpp`. | `render_systems.cpp` | Maintainability |
