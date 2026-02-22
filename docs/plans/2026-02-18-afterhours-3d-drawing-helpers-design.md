# Afterhours 3D Drawing Helpers Design

## Goal

Add backend-agnostic 3D drawing primitives to afterhours, following the same pattern as the existing 2D `drawing_helpers.h`. Both the raylib and sokol backends get implementations. This covers the shared 3D needs of endless_dance_chaos, pharmasea, and juggling.

## API Surface

### Camera3D Struct (in `afterhours::` namespace)

```cpp
struct Camera3D {
    Vector3Type position;
    Vector3Type target;
    Vector3Type up;
    float fovy;
    int projection; // 0 = perspective, 1 = orthographic
};
```

When `AFTER_HOURS_USE_RAYLIB` is defined, this is `using Camera3D = raylib::Camera3D;` (zero-cost, same memory layout). For sokol, it's the struct above.

### Functions (11 total)

```
begin_3d(Camera3D& cam)
end_3d()

draw_cube(Vector3 pos, float w, float h, float d, Color c)
draw_cube_wires(Vector3 pos, float w, float h, float d, Color c)
draw_plane(Vector3 center, Vector2 size, Color c)
draw_sphere(Vector3 pos, float radius, Color c)
draw_sphere_wires(Vector3 pos, float radius, int rings, int slices, Color c)
draw_cylinder(Vector3 pos, float rtop, float rbot, float h, int slices, Color c)
draw_cylinder_wires(Vector3 start, Vector3 end, float rtop, float rbot, int slices, Color c)
draw_line_3d(Vector3 start, Vector3 end, Color c)
get_world_to_screen(Vector3 pos, Camera3D cam) -> Vector2
```

## File Layout

```
afterhours/src/
  drawing_helpers_3d.h          # dispatcher (same pattern as drawing_helpers.h)
  backends/
    raylib/drawing_helpers_3d.h  # thin wrappers around raylib::Draw*
    sokol/drawing_helpers_3d.h   # sgl_v3f-based implementations
    none/drawing_helpers_3d.h    # no-op stubs
```

## Raylib Backend

Thin wrappers — each function is a single `raylib::Draw*` call. `Camera3D` is an alias for `raylib::Camera3D`. Example:

```cpp
inline void draw_cube(Vector3Type pos, float w, float h, float d, Color c) {
    raylib::DrawCube(pos, w, h, d, c);
}
```

## Sokol Backend

Uses `sokol_gl.h` immediate-mode 3D API:

- **`begin_3d`**: Save current matrix state. Switch to projection mode, load perspective or ortho matrix based on `cam.projection`. Switch to modelview, load lookat matrix from camera fields.
- **`end_3d`**: Restore the 2D orthographic projection set up by `begin_drawing()`.
- **`draw_cube`**: `sgl_begin_quads()` with 24 `sgl_v3f()` vertices (6 faces).
- **`draw_cube_wires`**: `sgl_begin_lines()` with 24 `sgl_v3f()` vertices (12 edges).
- **`draw_plane`**: Single quad via `sgl_begin_quads()` with 4 `sgl_v3f()` vertices on the XZ plane.
- **`draw_sphere`**: Latitude/longitude triangle strip subdivision (~16x16 segments).
- **`draw_sphere_wires`**: Same geometry as `draw_sphere` but with `sgl_begin_lines()`.
- **`draw_cylinder`**: Two circular caps + side quads via triangle strips.
- **`draw_cylinder_wires`**: Line loops for caps + vertical lines for sides.
- **`draw_line_3d`**: `sgl_begin_lines()` + two `sgl_v3f()` calls.
- **`get_world_to_screen`**: Manual MVP matrix multiply + viewport transform. Build view matrix from camera lookat, projection matrix from fovy/aspect, multiply, divide by w, map to screen coords.

## Migration for Game Code

After this lands in afterhours, the game's `gfx3d.h` becomes a thin include:

```cpp
#include "afterhours/src/drawing_helpers_3d.h"
```

The game-level `draw_cube`, `draw_plane`, etc. calls stay the same — they just resolve to afterhours functions instead of local wrappers. The `Camera3D` type in game components (`ProvidesCamera`, `CameraState`, `GameCam`) can use `afterhours::Camera3D` directly.

## Implementation Order

1. Create `drawing_helpers_3d.h` dispatcher
2. Implement raylib backend (straightforward 1:1 wrappers)
3. Implement sokol backend (most work — geometry generation)
4. Implement none backend (stubs)
5. Update game's `gfx3d.h` to delegate to afterhours
6. Test with existing E2E suite
