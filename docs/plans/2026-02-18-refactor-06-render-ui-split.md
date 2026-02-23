# Refactor 06: Break Up `RenderUISystem::once()`

## Problem

`RenderUISystem::once()` in `render_ui.cpp` is a 330-line monolithic method that handles:

1. **Top bar** (lines 179–267) — time, phase, speed controls, death count, attendees, day, events, FPS
2. **Build bar** (lines 277–329) — tool icons, selection, tooltips
3. **Toast messages** (lines 332–353) — fade and draw
4. **NUX banners** (lines 356–403) — hint display and dismiss button
5. **Compass** (lines 407–422) — camera direction indicator
6. **Hover info** (lines 425–481) — tile coordinates, agent breakdown, data layer label

It also mixes input handling (speed icon clicks at line 215, tool selection clicks at line 297, NUX dismiss click at line 398) with rendering.

## Proposed Fix

### Split into focused systems

Replace `RenderUISystem` with 6 smaller systems:

| New System | Responsibility | Approx lines |
|------------|---------------|--------------|
| `RenderTopBarSystem` | Time, phase, death, attendees, day, events, FPS | ~60 |
| `RenderBuildBarSystem` | Tool icons, selection highlight, tooltips | ~50 |
| `RenderToastsSystem` | Toast message fade and draw | ~20 |
| `RenderNuxBannerSystem` | Hint text, dismiss button | ~40 |
| `RenderCompassSystem` | Camera direction N indicator | ~15 |
| `RenderHoverInfoSystem` | Tile hover info, data layer label | ~40 |

### Move input handling to update systems

The click handlers for speed icons, build tool selection, and NUX dismiss should be update systems, not render systems:

- **Speed control clicks** → `SpeedControlSystem` (update)
- **Build tool clicks** → Already partially in `PathBuildSystem`, consolidate there
- **NUX dismiss clicks** → `NuxSystem` already handles dismiss logic, just needs click detection moved

### Shared text helpers

Keep `RenderUISystem::draw_text`, `measure_text`, `draw_text_bg`, `draw_text_centered` as free functions or in a `ui_helpers` namespace, since multiple systems use them.

## Files to change

- `src/render_ui.cpp` — Split `RenderUISystem` into 6 systems, extract input handlers
- `src/update_systems.cpp` — Add `SpeedControlSystem` for speed icon clicks
- `src/building_systems.cpp` — Absorb build bar click handling (if not already there)
- `src/render_helpers.h` — Move shared text helper functions here (or a new `ui_helpers.h`)

## Estimated impact

- **Performance:** No measurable change — same draw calls, just reorganized.
- **Complexity:** Each system is < 60 lines and has a single responsibility. Much easier to modify one UI element without risk to others. Input/render separation prevents subtle frame-ordering bugs.

## Risks

- More systems means more registration calls in `register_render_ui_systems()`. Minor boilerplate increase.
- Shared state (e.g., `bar_x` accumulator for top bar layout) is currently local to the monolithic function. Each sub-system that contributes to the top bar would need either fixed positions or a shared layout accumulator. Fixed positions are simpler and avoid coupling.
