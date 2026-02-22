#pragma once

#include "rl.h"
#include "afterhours/src/drawing_helpers_3d.h"

// Re-export afterhours 3D functions into global scope
using afterhours::begin_3d;
using afterhours::draw_cube;
using afterhours::draw_cube_wires;
using afterhours::draw_cylinder;
using afterhours::draw_cylinder_wires;
using afterhours::draw_line_3d;
using afterhours::draw_plane;
using afterhours::draw_sphere;
using afterhours::draw_sphere_wires;
using afterhours::end_3d;
using afterhours::get_world_to_screen;

// ── Render Textures (still raylib-specific) ──

inline raylib::RenderTexture2D load_render_texture(int w, int h) {
    return raylib::LoadRenderTexture(w, h);
}

inline void unload_render_texture(raylib::RenderTexture2D& rt) {
    raylib::UnloadRenderTexture(rt);
}

inline void begin_texture_mode(raylib::RenderTexture2D& rt) {
    raylib::BeginTextureMode(rt);
}

inline void end_texture_mode() { raylib::EndTextureMode(); }

inline void draw_render_texture(const raylib::RenderTexture2D& rt, float x,
                                float y, Color tint) {
    raylib::DrawTextureRec(
        rt.texture,
        {0, 0, (float) rt.texture.width, -(float) rt.texture.height}, {x, y},
        tint);
}

inline void draw_texture_rec(raylib::Texture2D tex, Rectangle src, vec2 pos,
                             Color tint) {
    raylib::DrawTextureRec(tex, src, pos, tint);
}
