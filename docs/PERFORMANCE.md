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

Next: see the draw-batching pass below.

## Draw batching in RenderIsoSystem (2026-10-19, headless, perf_1000_agents)

Afterhours has no batch helper (`drawing_helpers.h`'s `draw_quad` is one
`rlSetTexture` + `rlBegin(RL_QUADS)` per quad, and `draw_rectangle` is one
`DrawRectangleRec` per rect). `iso_draw_poly4` /
`iso_draw_rect` in [render_world.cpp](/Users/gabeochoa/p/endless-dance-chaos/src/render_world.cpp)
now emit every iso quad / rect as two `RL_TRIANGLES` in ONE
`rlBegin`/`rlEnd` (direct `rlgl` calls, one `capture::record` per
logical quad/rect, same as before). rlgl coalesces consecutive
same-mode, same-texture primitives into a single draw entry, so a run
of tiles / peeps / buildings that used to flip `QUADS <-> TRIANGLES`
at every ellipse/circle/rect boundary now stays in `TRIANGLES` until
a textured sprite, text glyph, line, or rect-outline intervenes
(those are few per frame: sprites/text for bubbles + sign glyphs,
lines for death-marker X / rain, `draw_rect_lines` for sign-board
outlines, which stay `RL_QUADS`). Painter order is unchanged:
primitives are submitted to rlgl in exactly the same order, and the
sort, item construction, and cull are untouched. Silhouette rects
(`draw_iso_silhouette`) stay `RL_QUADS` — few per frame, not worth
the churn.

Winding is the whole correctness story, and the old comment in
`render_world.cpp` was half-wrong: `RL_TRIANGLES` does rasterize in
this pass, but only with `draw_quad`'s winding. The triangles are
`(d, c, b) + (d, b, a)` — the triangles `draw_quad`'s QUADS index
buffer `(0,1,2, 0,2,3)` over its reversed vertex order produced. The
Metal split `(a, b, c) + (a, c, d)` is back-facing here and draws
nothing (verified: flipping the winding makes
[43_iso_batch.e2e](/Users/gabeochoa/p/endless-dance-chaos/tests/e2e/43_iso_batch.e2e)
fail 4 of its 7 pixel asserts, and tiles read as sky). The Metal path
keeps its own split.

### Before / after (same harness, same machine, `perf_1000_agents.e2e`, 2 runs each)

| Run | fps avg | P99 frame | RenderIsoSystem mean | EndRenderSystem mean |
| --- | ------- | --------- | -------------------- | -------------------- |
| BEFORE 1 | 42.8 | 39.99ms | 12.78ms | 1.49ms |
| BEFORE 2 | 43.2 | 29.06ms | 12.65ms | 1.46ms |
| AFTER 1 | 43.4 | 27.42ms | 12.39ms | 1.27ms |
| AFTER 2 | 43.8 | 28.69ms | 12.29ms | 1.35ms |

Means: fps 43.0 -> 43.6 (+1.4%), RenderIsoSystem 12.72ms -> 12.34ms
(-0.38ms, -3.0%). Both AFTER runs beat both BEFORE runs on
RenderIsoSystem, but the fps gain is within run-to-run noise and P99
is noisy (BEFORE range 29–40ms). Honest read: batching removes the
per-quad texture/mode switches and a chunk of per-primitive calls,
but it does not reduce the vertex count — the draw loop is still
dominated by the circle/ellipse fans (a peep head is 3 circles x 36
segments = ~324 vertices; tiles are ~4 vertices per quad). Cutting
RenderIsoSystem further needs fewer vertices (e.g. coarser fans for
small circles, at the cost of pixel changes), not more batching.

### Visuals preserved (pixel diff, BEFORE vs AFTER screenshots)

- `17_iso_default`: 0 differing pixels in the world viewport
  (x < 1130, 44 <= y <= 644). Total diff 584 px, all sidebar +
  bottom ticker — the same pattern as a same-binary AFTER-vs-AFTER
  diff (668 px, 0 world).
- `populated_200`: 22,951 differing pixels, 22,145 in the world — but
  a same-binary AFTER-vs-AFTER run differs by 22,680 (21,810 world).
  The 200 agents' positions vary with real frame timing (sim
  nondeterminism); batching is render-only and cannot move agents.
  `17_iso_default` (no moving agents) is the clean proof: 0 world diffs.
- Regression test [43_iso_batch.e2e](/Users/gabeochoa/p/endless-dance-chaos/tests/e2e/43_iso_batch.e2e):
  canopy + stage-roof + tile + sign-board pixels, a peep-rect pixel
  over its tile, and a density-overlay pixel that must read as
  blended overlay, not the plain tile. Flipping the triangle winding
  makes 4 of its 7 pixel asserts fail (verified); the fixed build
  passes.

### Suite

Full headless suite (`--test-script-dir tests/e2e`): 105 scripts, 104 PASS
including `43_iso_batch`. The one failure is `vip_eventual`, the same
intermittent timing flake as in the cull pass (see above): it fails on
the same `eventually_assert_agent_watching` timeout and passes
standalone, unrelated to rendering (batching writes no sim state).

Next: fewer fan vertices for small circles/ellipses, or accepting the
~12.3ms RenderIsoSystem floor for this vertex count.

## Felt speed: launch, frame at scale, pan, tab switch (2026-10-19, headless)

Three felt-speed axes, measured before the one fix below. Launch uses
new env-gated instrumentation in [main.cpp](/Users/gabeochoa/p/endless-dance-chaos/src/main.cpp):
`DANCE_PERF=1` logs
`[PERF] launch_to_first_frame_ms=... first_input_ms=... graphics_init_ms=...`
(main entry -> first completed frame, main entry -> start of the first
input-sampled frame, main entry -> graphics init done, i.e. the backend
created the window/GL context before `cfg.init` runs).
Without `DANCE_PERF`, zero `[PERF]` lines (verified). Pan / tab-switch
use separate process runs per window (`perf_start` does not reset the
profiler, so a single run's `dump_profile` means are cumulative).

### STEP 1 baselines (before the fix)

Launch (tiny `wait_frames 2` script, `DANCE_PERF=1`):

| Metric (median) | BEFORE (n=6) | First 3 runs |
| --- | --- | --- |
| launch_to_first_frame | 530.2ms | 486.2ms |
| first_input | 441.9ms | 352.2ms |
| graphics_init (window/GL creation) | 256.7ms | 217.5ms |
| cfg.init (first_input − graphics_init) | 179.0ms | 138.3ms |
| frame 0 (launch − first_input) | 107.5ms | 126.0ms |

Temporary probes (removed) attributed the launch: `InitAudioDevice`
median ~172ms (8 samples, 131–299ms), render texture ~0.1–0.3ms,
`register_systems` ~0.1–0.6ms, `make_sophie` / world gen over all
10,404 tiles 0.33–0.45ms — world gen is NOT the offender — script load
0.1–10.6ms. Frame 0: first `EndRender` present ~86–107ms (derived:
4×mean over frames 0–3 minus 3×steady mean; backend/window-server
first-present cost, not fixable here), font load ~14ms, sprite loads
~1–7ms (one 37ms outlier), minimap first build ~1.4–2.0ms.

Frame at scale (`perf_1000_agents.e2e`, 1 run): 44.5 fps avg,
RenderIsoSystem 12.08ms, EndRenderSystem 1.18ms,
RenderParkWindowSystem 0.74ms, P99 31.20ms.

Camera pan (200 agents, `set_camera_target` sweeps across the big
park, `wait_frames 2` per step):

| Window | fps avg (samples) | RenderIsoSystem | EndRenderSystem | P99 |
| --- | --- | --- | --- | --- |
| Static | 105.7 (166) | 5.25ms | 1.69ms | 12.66ms |
| Panning | 113.9 (405) | 4.43ms | 1.51ms | 12.67ms |

Panning adds no frame cost at 200 agents (slightly faster: some pan
viewpoints hold fewer on-screen agents/tiles; within noise).

Park window tab switch (window is always open):

| Window | RenderParkWindowSystem mean | fps avg |
| --- | --- | --- |
| Steady (Park tab) | 0.11ms (174 calls) | 136.2 |
| Switching tab every 2 frames | 0.15ms (730 calls) | 128.0 |

Switching costs +0.04ms/frame — every tab page is cheap text/rects.
Latency: a tab click renders the new tab's content on the NEXT frame
(`RenderParkWindowSystem` consumes the click and flips + draws the new
page in one `once()`). Probe (`click` tab, `wait_frames 1`,
`expect_text` for the new tab's text) passes; a negative control with
nonexistent text fails with `[TIMEOUT] expect_text`. Caveat:
`expect_text` retries across frames, so the probe alone gives an upper
bound — the 1-frame bound is from the code path.

### STEP 2: one fix — lazy audio init

Biggest fixable offender was `InitAudioDevice` (~172ms median) running
at startup in `main`, before the first frame, in every mode — while in
test mode the audio was never even used (`get_audio().init()` was
already skipped there). The fix, in
[audio.h](/Users/gabeochoa/p/endless-dance-chaos/src/audio.h):

- `AudioManager::init()` now owns the device: it calls
  `InitAudioDevice()` itself.
- New `ensure_init()`: first real sound pays the device + wave-synthesis
  cost, launch pays nothing. In test mode it is a no-op (e2e stays
  silent).
- `play_*` / `start_music` call `ensure_init()` then return without
  touching raylib sound APIs while still uninitialized (no device
  exists in that case); `update()` guards on `initialized`;
  `shutdown()` closes the device when initialized. `main` no longer
  inits audio at startup or closes the device unconditionally at exit.
- Trade-off: the deferred cost moves to the first real sound in a
  player run. Measured once (temporary call, removed): full `init()`
  (device + waves) = 264.8ms — that is the first-sound hitch.

Regression test [44_audio_lazy.e2e](/Users/gabeochoa/p/endless-dance-chaos/tests/e2e/44_audio_lazy.e2e)
plus the `assert_audio_uninitialized` E2E command: after startup, and
after a refusal toast (which calls `play_toast`), audio must still be
uninitialized in test mode. Removing the test-mode guard from
`ensure_init()` makes the post-toast assert fail (verified by mutation);
the fixed build passes.

### A/B (launch, `DANCE_PERF=1`, same harness)

| Metric (median) | BEFORE | AFTER | Delta |
| --- | --- | --- | --- |
| cfg.init, 3-run | 138.3ms | 0.8ms | −137.5ms |
| cfg.init, extended (n=6 / n=8) | 179.0ms | 0.7ms | −178.3ms (−99.6%) |
| launch total, 3-run | 486.2ms | 650.9ms | +164.7ms (noise, see below) |
| launch total, extended | 530.2ms | 481.6ms | −48.6ms (within noise) |
| graphics_init, extended | 256.7ms | 351.6ms | +94.9ms (machine drift/noise) |
| frame 0, extended (control) | 107.5ms | 117.7ms | unchanged within noise |

Honest read: total launch is dominated by window/GL creation variance
(±300ms run-to-run, one 1111ms outlier), so the total-launch median
move is not significant and no total-launch win is claimed beyond the
init component. The isolated effect is unambiguous: `cfg.init` goes
from ~140–180ms to <1ms because audio init no longer runs at startup.
Frame 0 (the control — untouched by the fix) is unchanged.

### Suite

Full headless suite (`perl -e 'alarm 1500; exec @ARGV'
./output/dance.exe --test-mode --headless --test-script-dir tests/e2e`,
log `/tmp/dance_suite_perf.log`): 106 scripts, 106 PASS, 0 failed,
including `44_audio_lazy` and `vip_eventual` (the known intermittent
flake, passed this run). No probe leftovers: the temporary step /
frame-0 / font / sprite / minimap probes were all removed (grep-clean);
only the `DANCE_PERF=1` launch line in `main.cpp` remains. Nothing
committed.
