# Performance (felt speed — Gabe 2026-10-18)
## Baseline 2026-10-19 (headless profile, perf_1000_agents suite section)
- 1000 agents: fps avg 40.1 (min 37); profiled scene: Avg FPS 61.8,
  P99 frame 28.22ms. Top costs: RenderIsoSystem 4.17ms mean,
  EndRenderSystem 3.06ms mean (12928 calls), ticker 0.31, minimap 0.17,
  park window 0.13. Render is ~85% of measured frame cost.
Next: cull off-screen iso tiles/peeps in RenderIsoSystem (viewport cull),
measure at 1000 agents before/after. One fix, one commit, gate.
