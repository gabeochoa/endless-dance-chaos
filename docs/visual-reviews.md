# Visual reviews (independent reviewer: gpt-6-sol, 2026-10-18)

Rule (Gabe 2026-10-18): screenshots reviewed by ANOTHER model each visual milestone.

## mailboy — fidelity D, readability D (vs three.js mocks)
Reviewer saw real captures: dim corridor, NO visible HUD (note: headless captures
lack HUD glyphs — raylib default font doesn't load headless; windowed probes DO
show HUD. Partly a capture artifact, partly real: brightness/contrast low).
Top fixes: (1) big camera-facing nameplates visible before throw window,
(2) HUD prominence (next delivery + beat cue), (3) brightness/value separation,
(4) visible parcel arc + grade near recipient, (5) basement as labeled sorting
station. Mock flaw: cart/parcel too big in mock.

## endless dance chaos — fidelity C, readability C (vs RCT2 mocks)
Top fixes: (1) frame the park footprint in the usable area, (2) subdue mint tile
seams + lavender dancefloor dominance, darker grass, (3) smaller movable window,
clear viewport, (4) building silhouettes (canopy/speakers/WC sign/awning/medic
cross), (5) peep head/body + shadows, selective bubbles. Mock flaw: mock peeps
are confetti too; use full-island framing as target.

## 2026-10-18 dance re-review (gpt-6-sol): fidelity D+, readability C- (DOWN from C/C)
Reviewer: "debug map, not an RCT2 park." Framing partial (thin diamond, dead
space, attractions tiny); seams still read as stitching; lavender slab
conspicuous; silhouettes NOT landed (blocks still need labels); peeps not
visible at this zoom (20 attendees = tiny cluster). Root tension: whole-park
framing vs sprite readability — RCT2 solves it with bigger multi-tile sprites
+ closer default zoom on a park AREA, not the whole map. Next pass: larger
multi-tile building sprites with roofs/shadows, textured ground (noise/varied
tiles, no seam grid), dance floor as bordered plaza tiles, peeps bigger with
contrast at default zoom; consider default view = stage area, minimap for
whole park. Do not call visuals done until re-review improves.
