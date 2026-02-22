# Backend Decoupling: Raylib → Afterhours Rendering APIs

**Date:** 2026-02-18
**Goal:** Decouple game rendering from direct raylib calls so the codebase could theoretically run on either the raylib or Sokol/Metal backend.

## Current State

The game calls `raylib::` directly in ~360 sites across rendering, input, and audio. The afterhours framework already provides backend-agnostic abstractions for 2D drawing, windowing, and input — but the game bypasses them.

## Architecture

Three layers after migration:

1. **afterhours drawing API** — All 2D rendering (`draw_rectangle`, `draw_line`, `draw_text`, `draw_circle`, `begin_scissor_mode`). Already exists in both raylib and Sokol backends.

2. **Game-level `gfx3d.h`** — Thin wrapper for 3D primitives, render textures, and camera. Delegates to raylib today. Only ~8 distinct 3D functions used.

3. **`rl.h` becomes backend config** — Sets `AFTER_HOURS_USE_RAYLIB`, vector typedefs, and includes. Game code no longer calls `raylib::` directly. Only `gfx3d.h` internals touch raylib.

## Type Migration

| Type | Current | After |
|------|---------|-------|
| Color | `raylib::Color{r,g,b,a}` | `Color{r,g,b,a}` (= `afterhours::Color`, same struct) |
| Named colors | `raylib::WHITE` | `Color{255,255,255,255}` or `colors::white()` |
| Vector2 | `raylib::Vector2` / `vec2` | Keep `vec2` typedef, backend-conditional |
| Vector3 | `raylib::Vector3` / `vec3` | Keep `vec3` typedef, backend-conditional |
| Rectangle | `raylib::Rectangle` / `Rectangle` | Keep `Rectangle` typedef via `RectangleType` |
| Font | `raylib::Font` / `get_font()` | Keep game-level `get_font()`, backend handles internally |

## Migration Steps

Each step keeps the game fully functional and testable (run full E2E suite after each).

### Step 1: Color migration (~150 sites)

Replace all `raylib::Color{...}` → `Color{...}` and `raylib::WHITE` → `Color{255,255,255,255}`.

- Add `using Color = afterhours::Color;` to game headers (already the case when `AFTER_HOURS_USE_RAYLIB` is defined)
- Pure find-and-replace, no behavior change

### Step 2: 2D drawing migration (~80 sites)

Replace direct raylib 2D calls with afterhours equivalents:

| raylib call | afterhours equivalent |
|-------------|----------------------|
| `raylib::DrawRectangle(x,y,w,h,c)` | `afterhours::draw_rectangle({x,y,w,h}, c)` |
| `raylib::DrawRectangleLines(x,y,w,h,c)` | `afterhours::draw_rectangle_outline({x,y,w,h}, c)` |
| `raylib::DrawLine(x1,y1,x2,y2,c)` | `afterhours::draw_line(x1,y1,x2,y2,c)` |
| `raylib::DrawCircle(x,y,r,c)` | `afterhours::draw_circle(x,y,r,c)` |
| `raylib::DrawTextEx(font,text,pos,sz,sp,c)` | `afterhours::draw_text_ex(font,text,pos,sz,sp,c)` |
| `raylib::BeginScissorMode(x,y,w,h)` | `afterhours::begin_scissor_mode(x,y,w,h)` |
| `raylib::EndScissorMode()` | `afterhours::end_scissor_mode()` |
| `raylib::MeasureTextEx(font,text,sz,sp)` | Keep as-is (font system is backend-specific) |

### Step 3: Window/frame/timing (~15 sites)

Replace lifecycle and query calls:

| raylib call | afterhours equivalent |
|-------------|----------------------|
| `raylib::BeginDrawing()` | `afterhours::graphics::begin_drawing()` |
| `raylib::EndDrawing()` | `afterhours::graphics::end_drawing()` |
| `raylib::ClearBackground(c)` | `afterhours::graphics::clear_background(c)` |
| `raylib::GetFPS()` | `afterhours::graphics::get_fps()` |
| `raylib::GetTime()` | `afterhours::graphics::get_time()` |
| `raylib::GetFrameTime()` | `afterhours::graphics::get_frame_time()` |
| `raylib::InitWindow(...)` | `afterhours::graphics::init_window(...)` |
| `raylib::CloseWindow()` | `afterhours::graphics::close_window()` |
| `raylib::WindowShouldClose()` | `afterhours::graphics::window_should_close()` |
| `raylib::SetTargetFPS(n)` | `afterhours::graphics::set_target_fps(n)` |
| `raylib::SetExitKey(k)` | `afterhours::graphics::set_exit_key(k)` |
| `raylib::SetTraceLogLevel(l)` | `afterhours::graphics::set_trace_log_level(l)` |

### Step 4: Create `gfx3d.h` wrapper (~25 sites)

New file `src/gfx3d.h` with a `gfx3d` namespace:

```cpp
namespace gfx3d {
    void begin_3d(const Camera& cam);
    void end_3d();
    void draw_plane(vec3 pos, vec2 size, Color color);
    void draw_cube(vec3 pos, float w, float h, float d, Color color);
    void draw_cylinder(vec3 pos, float rtop, float rbot, float h, int slices, Color color);
    void draw_sphere(vec3 pos, float radius, Color color);
    void draw_line3d(vec3 start, vec3 end, Color color);
    vec2 world_to_screen(vec3 pos, const Camera& cam);

    // Render textures
    RenderTexture load_render_texture(int w, int h);
    void begin_texture_mode(RenderTexture& rt);
    void end_texture_mode();
    void draw_texture_rec(Texture tex, Rectangle src, vec2 pos, Color tint);
    void unload_render_texture(RenderTexture& rt);
}
```

Implementation delegates to raylib. Camera and RenderTexture types become backend-conditional (raylib types when `AFTER_HOURS_USE_RAYLIB`, stubs otherwise).

### Step 5: Clean up `rl.h`

- Remove direct `#include <raylib.h>` from `rl.h` (moved into `gfx3d.h` internals)
- `rl.h` becomes: set `AFTER_HOURS_USE_RAYLIB`, define `Vector2Type`/`RectangleType`/`TextureType`, include `afterhours/ah.h`
- Verify no game `.cpp` file references `raylib::` directly
- Audio stays isolated in `audio.h` (already encapsulated, separate migration if needed)

## Out of Scope

- **Audio abstraction** — `audio.h` already encapsulates all audio. Can be migrated separately.
- **Sokol 3D implementation** — `gfx3d.h` will stub/no-op on Sokol. Real 3D rendering on Sokol is a future project.
- **Input migration** — Game already uses afterhours `input::` system for most input. Only `MOUSE_BUTTON_LEFT/RIGHT` constants need aliasing.

## Validation

Run the full E2E test suite (60 scripts, 149 screenshots) after each step. Compare screenshots against `screenshots/opengl/` baseline to catch rendering regressions.
