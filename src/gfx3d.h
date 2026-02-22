#pragma once

#include "rl.h"
#include "afterhours/src/drawing_helpers.h"
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

// Re-export afterhours render texture functions into global scope
using afterhours::begin_texture_mode;
using afterhours::capture_render_texture;
using afterhours::capture_render_texture_to_memory;
using afterhours::capture_screen_to_memory;
using afterhours::draw_render_texture;
using afterhours::draw_texture_rec;
using afterhours::end_texture_mode;
using afterhours::load_render_texture;
using afterhours::unload_render_texture;
