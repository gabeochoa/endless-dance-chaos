# Performance (felt speed — Gabe 2026-10-18)
## Viewport cull in RenderIsoSystem (2026-10-19, headless, perf_1000_agents)

`RenderIsoSystem` now culls by projected screen position BEFORE item
construction ([render_world.cpp](/Users/gabeochoa/p/endless-dance-chaos/src/render_world.cpp)):
tiles/buildings (full-screen + margin: 2.5 tile-widths x, 5 tile-heights + 4px
y, covering 4x4 footprints + roof extras), peeps (asymmetric: bubble reach
above, shadow below), staff (previously unculled), death markers/particles,
build preview/hover, and the night-glow pass (non-glow tiles skipped before
projecting). Kept items are constructed in the same order as before. It
replaces the agents' loose `VisibleRegion` grid check with the exact screen
test (same projection the draw uses). `BeginRenderSystem`'s `VisibleRegion`
is the axis-aligned bbox of the viewport corners: at default zoom it
iterates 5192 tiles while the cull keeps ~4191 (19% culled).

### Before / after (same harness, same machine, `perf_1000_agents.e2e`, 2 runs each)

| Run | fps avg | P99 frame | RenderIsoSystem mean | EndRenderSystem mean |
| --- | ------- | --------- | -------------------- | -------------------- |
| BEFORE 1 | 44.3 | 27.17ms | 12.74ms | 1.02ms |
| BEFORE 2 | 44.0 | 35.47ms | 12.77ms | 1.09ms |
| AFTER 1 | 43.6 | 29.47ms | 12.61ms | 1.22ms |
| AFTER 2 | 45.5 | 29.90ms | 12.05ms | 1.09ms |

Means: fps 44.2 -> 44.6 (within run-to-run noise), RenderIsoSystem
12.76ms -> 12.33ms (-0.43ms, -3.4%), P99 and EndRender unchanged within
noise. The earlier baseline in this file (fps 40.1, P99 28.22ms,
RenderIso 4.17ms, EndRender 3.06ms) was a different profiling context and
is superseded by this same-harness A/B.

### Why the gain is small

A temporary probe (instrumentation, removed) split `RenderIsoSystem` as:
draw loop ~6.8ms, sort ~1.9ms, item construction ~0.7ms. The remaining
cost is draw calls for ON-SCREEN items (1000 peeps x ~8 primitives each,
~4200 kept tiles), which no culling can remove. The next lever would be
draw batching, not more culling — not attempted here.

### Visuals preserved (pixel diff, BEFORE vs AFTER screenshots)

- `17_iso_default`: 0 differing pixels in the world viewport
  (x < 1130, 44 <= y <= 644). All 527 diffs are sidebar (245) + bottom
  ticker strip (282) — the same pattern as a same-binary run-to-run
  diff (454 px), i.e. nondeterministic UI, not the cull.
- `populated_200`: 22,825 differing pixels, 21,948 in the world — but a
  same-binary AFTER-vs-AFTER run differs by 23,569 (22,712 world). The
  200 agents' positions vary with real frame timing (sim
  nondeterminism); the cull is render-only and cannot move agents.
  `17_iso_default` (no moving agents) is the clean proof: 0 world diffs.
- Regression test [42_iso_cull.e2e](/Users/gabeochoa/p/endless-dance-chaos/tests/e2e/42_iso_cull.e2e):
  stage speaker-tower sliver from an off-screen anchor + a frozen
  30-guest bubble cloud from off-screen peeps. Zeroing the cull margins
  makes both cases fail (verified); the fixed build passes.

### Suite

Full headless suite (`--test-script-dir tests/e2e`): 104 scripts, 103 PASS
including `42_iso_cull`. The one failure is `vip_eventual`, an intermittent
timing flake unrelated to this change: it fails on the same
`eventually_assert_agent_watching` 20s timeout in 2 of 4 suite runs, passes
in the other 2 and standalone (2x), with behaviorally identical code —
a VIP walk-back vs. crowd timing issue, not rendering (the cull writes no
sim state).

Next: batch on-screen iso draw calls (peeps/tiles) to move RenderIsoSystem
below ~12ms; no further culling available.
