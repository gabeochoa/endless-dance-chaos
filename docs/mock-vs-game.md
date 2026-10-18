# Mock vs Game — endless dance chaos (2026-10-17)

Target: RCT2 style (Gabe correction) — `mocks/threejs/index.html` (#iso,
`close.html`): 2:1 isometric, floating tabbed windows, bottom toolbar,
money floaters, peeps with thought bubbles, height steps.
Mock PNGs blocked in the boulder sandbox (bootstrap_check_in 1100).
Real-game capture: tests/e2e/screenshots/tycoon_02_built_festival.png.

## Ranked gaps (impact order)
1. **Projection**: game is flat top-down; RCT2 target is 2:1 isometric with
   4-way rotation. Biggest single change: iso tile rendering of the existing
   grid (data is already tile-based), fixed rotations.
2. **Peeps**: game agents are colored cubes with pips; target has tiny readable
   peeps + thought bubbles (needs). Add bubble icons over agents with urgent
   needs + slightly articulated sprites (head/body).
3. **Windows UI**: game HUD is bars; target is floating tabbed windows (Park /
   Finances / Guests / Staff / Lineup). Reframe sidebar panels as RCT2 windows
   with title bars + tabs.
4. **Terrain & scenery**: target has height steps, path/queue variety, scenery.
   Add height to expansion plots + decorative scenery placeables.
5. **Money floaters**: target pops +$20 over earners. Add floating cash text on
   ticket/bar income (already have floating-text patterns in ticker/toasts).

Next chunks: (1) iso projection pass, (2) peep bubbles, (3) window-framed panels.

## Update 2026-10-18 (iso framing + RCT2 windows + money floaters)

- Gap #1 framing: default iso zoom is now distance 18 (80px tiles,
  ~32 tile-steps across 1280px / ~28 across the 1130px main view),
  centred on the park centre; `reset_game` restores zoom + target so
  `17_iso_default` is deterministic. Wheel / pinch zoom unchanged.
- Gap #3 windows: the stacked sidebar blocks are one floating tabbed
  window (Park / Finances / Guests / Staff / Lineup) with a title bar
  (`Endless Dance Chaos`, FPS, close/minimize deco). All prior labels
  + buttons kept (booking rows, hire/fire, price buttons); the bottom
  bar stays the toolbar with a rating + cash + day readout at its
  right, over the minimap's Locked-land strip.
- Gap #5 floaters: `+$N` rising/fading text at the gate on ticket
  sale and at the Bar on service, via world->screen in the active
  projection; at most 4 alive, same-anchor sales within 1s merge
  (amounts sum). E2E: `expect_text "+$N"` or `assert_floater_count`.

## Update 2026-10-17 (real mock renders)
Mock PNGs rendered on devvm48270 (swiftshader-webgl; single-file bundles
for capture, repo mocks stay modular). The iso grid, tabbed windows,
ticker, thought bubbles and money floaters all read clearly at 1280x720 —
gap ranking stands; iso projection remains #1.
