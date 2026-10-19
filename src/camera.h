
#pragma once

#include "game.h"
#include "gfx3d.h"
#include "input_mapping.h"
#include "log.h"
#include "rl.h"

// RCT-style isometric camera with 90-degree rotation support.
// Two view modes share one target / distance / rotation:
//  - iso_mode=true (default): pure 2D 2:1 diamond projection, computed in
//    iso_project()/iso_screen_to_world() — no 3D camera is consulted.
//  - iso_mode=false: the legacy 3D orthographic camera ("top-down").
// Rotation is the same rotation_index in both modes, so Q/E and the HUD
// button mean the same thing whichever view is active.
struct IsometricCamera {
    afterhours::Camera3D camera;

    // True = 2D iso diamonds; false = legacy 3D orthographic view.
    bool iso_mode = true;

    // Center of the world viewport (top bar above, build bar + ticker
    // below, sidebar to the right): x in [0, 1130], y in [44, 644].
    static constexpr float ISO_CENTER_X = (DEFAULT_SCREEN_WIDTH - 150) / 2.0f;
    static constexpr float ISO_CENTER_Y = (44.0f + 644.0f) / 2.0f;

    // Default iso zoom: the STAGE AREA framed inside the usable
    // viewport (x in [0, 1130], y in [44, 644]) — stage + dance
    // floor + nearby facilities — not the whole 51-tile park. At
    // distance 36 the tile is 40px wide, so the 1130px viewport
    // spans ~28 tiles across; the stage-area bounding box
    // (x 15..40, z 19..35: medtent, bathroom, food, stage, dance
    // floor) spans ~820x410px around the default target, with
    // room for the stage canopy / labels above it. Whole-park
    // framing is the zoom-OUT limit only: at max_distance 80 the
    // 52-tile fence diamond spans ~936x468px, so zooming out +
    // minimap-clicking the park centre shows the whole park.
    // The player can still zoom in to buildings / peeps
    // (distance 18) via wheel / pinch; the default stays a
    // framing, not a zoom level.
    static constexpr float DEFAULT_DISTANCE = 36.0f;

    // Default camera target: centre of the stage-area bounding box
    // (x 15..40, z 19..35), i.e. just west of the stage centre
    // (32, 27) so the stage, dance floor and the three pre-placed
    // facilities (medtent 15,25; bathroom 20,20; food 20,30) all
    // sit in frame. The gate (0, 26) still peeks in at the left
    // edge; the far fence corners are out of frame at this zoom.
    static constexpr float DEFAULT_TARGET_X = 27.5f;
    static constexpr float DEFAULT_TARGET_Z = 27.0f;

    // Camera distance and angle settings
    float distance = DEFAULT_DISTANCE;
    float min_distance = 5.0f;
    float max_distance = 80.0f;
    float scroll_sensitivity = 2.0f;

    // Isometric angle (typically ~35 degrees from horizontal)
    float pitch = -35.0f * DEG2RAD;

    // Rotation around Y axis (0, 90, 180, 270 degrees for N/E/S/W)
    float yaw = 45.0f * DEG2RAD;  // Start at 45 for classic iso look
    int rotation_index = 0;       // 0=NE, 1=SE, 2=SW, 3=NW

    // Target position (what camera looks at)
    vec3 target = {0.0f, 0.0f, 0.0f};

    // Pan speed
    float pan_speed = 10.0f;

    IsometricCamera() {
        camera.up = {0.0f, 1.0f, 0.0f};
        camera.fovy = distance;  // Orthographic "zoom" level matches distance
        camera.projection = afterhours::camera::ORTHOGRAPHIC;
        update_camera_position();
    }

    void update_camera_position() {
        // Calculate camera position based on distance, pitch, and yaw
        float cos_pitch = cosf(pitch);
        float sin_pitch = sinf(pitch);
        float cos_yaw = cosf(yaw);
        float sin_yaw = sinf(yaw);

        camera.position = {target.x + distance * cos_pitch * sin_yaw,
                           target.y + distance * -sin_pitch,
                           target.z + distance * cos_pitch * cos_yaw};
        camera.target = target;
    }

    // Rotate view 90 degrees clockwise (like RCT)
    void rotate_clockwise() {
        rotation_index = (rotation_index + 1) % 4;
        yaw = (45.0f + rotation_index * 90.0f) * DEG2RAD;
        update_camera_position();
        log_info("Camera rotated to {} degrees", 45 + rotation_index * 90);
    }

    // Rotate view 90 degrees counter-clockwise
    void rotate_counter_clockwise() {
        rotation_index = (rotation_index + 3) % 4;  // +3 is same as -1 mod 4
        yaw = (45.0f + rotation_index * 90.0f) * DEG2RAD;
        update_camera_position();
        log_info("Camera rotated to {} degrees", 45 + rotation_index * 90);
    }

    // For an orthographic camera fovy IS the zoom level, so distance and fovy
    // move together.
    void set_distance(float d) {
        distance = std::clamp(d, min_distance, max_distance);
        camera.fovy = distance;
        update_camera_position();
    }

    // Zoom in/out by a wheel notch
    void zoom(float delta) {
        set_distance(distance - delta * scroll_sensitivity);
    }

    // Trackpad pinch, which is multiplicative rather than per-notch: +0.01 is
    // "grow 1%", and growing the view means a smaller ortho distance. The
    // factor floor keeps a wild delta from flipping the sign.
    void pinch_zoom(float delta) {
        set_distance(distance / std::max(0.1f, 1.f + delta));
    }

    // ── 2D iso projection ──────────────────────────────────────────
    // Tile size in px, driven by the shared zoom (distance): 40px at
    // the default distance of 36, 2:1 diamonds (TILE_H = TILE_W / 2).
    float iso_tile_w() const { return 1440.0f / distance; }
    float iso_tile_h() const { return iso_tile_w() * 0.5f; }

    // Rotate grid coords into display coords. rotation_index 0/90/180/270
    // (Q/E steps). The inverse is unrotate_grid; the two must stay exact
    // inverses or picking drifts after a rotation.
    std::pair<float, float> rotate_grid(float x, float z) const {
        switch (rotation_index & 3) {
            case 1:
                return {z, -x};
            case 2:
                return {-x, -z};
            case 3:
                return {-z, x};
            default:
                return {x, z};
        }
    }
    std::pair<float, float> unrotate_grid(float dx, float dz) const {
        switch (rotation_index & 3) {
            case 1:
                return {-dz, dx};
            case 2:
                return {-dx, -dz};
            case 3:
                return {dz, -dx};
            default:
                return {dx, dz};
        }
    }

    // World (continuous grid coords) -> screen: sx=(dx-dz)*TILE_W/2,
    // sy=(dx+dz)*TILE_H/2 about the camera target, in display coords.
    vec2 iso_project(float wx, float wz) const {
        auto [dx, dz] = rotate_grid(wx, wz);
        auto [tx, tz] = rotate_grid(target.x, target.z);
        float tw = iso_tile_w();
        float th = iso_tile_h();
        return {ISO_CENTER_X + (dx - tx) * tw * 0.5f,
                ISO_CENTER_Y + (dx + dz - tx - tz) * th * 0.5f};
    }

    // Screen -> continuous world coords: exact inverse of iso_project.
    vec2 iso_screen_to_world(float sx, float sy) const {
        float tw = iso_tile_w();
        float th = iso_tile_h();
        auto [tx, tz] = rotate_grid(target.x, target.z);
        float a = 2.0f * (sx - ISO_CENTER_X) / tw;  // dx - tx
        float b = 2.0f * (sy - ISO_CENTER_Y) / th;  // (dx+dz) - (tx+tz)
        float dx = tx + a;
        float dz = tz + b - a;
        auto [wx, wz] = unrotate_grid(dx, dz);
        return {wx, wz};
    }

    // World (3D, y = height) -> screen. In iso mode the height lifts the
    // point up by y * TILE_H; in 3D mode this is the legacy projection.
    vec2 world_to_screen(vec3 w) const {
        if (!iso_mode) return get_world_to_screen(w, camera);
        vec2 s = iso_project(w.x, w.z);
        s.y -= w.y * iso_tile_h();
        return s;
    }

    // Pan the camera target
    void pan(float dx, float dz) {
        if (iso_mode) {
            // dx = screen-right, dz = screen-down (WASD move_x/move_z).
            // Screen-right is (1,-1) and screen-down is (1,1) in display
            // coords; unrotate back to grid coords to move the target.
            float ddx = dx + dz;
            float ddz = -dx + dz;
            auto [wx, wz] = unrotate_grid(ddx, ddz);
            target.x += wx;
            target.z += wz;
            update_camera_position();
            return;
        }
        // Adjust pan direction based on current rotation
        float cos_yaw = cosf(yaw);
        float sin_yaw = sinf(yaw);

        target.x += dx * cos_yaw + dz * sin_yaw;
        target.z += -dx * sin_yaw + dz * cos_yaw;
        update_camera_position();
    }

    // Handle input for camera controls
    void handle_input(float dt) {
        // View toggle: V switches iso <-> top-down (3D).
        if (action_pressed(InputAction::ToggleIsoView)) {
            iso_mode = !iso_mode;
            log_info("View mode: {}", iso_mode ? "ISO" : "TOP");
        }

        // Rotation: Q and E keys. Snaps to 0/90/180/270 in both modes:
        // rotation_index is the single source of truth, and the iso
        // projection reads it directly.
        if (action_pressed(InputAction::CameraRotateLeft)) {
            rotate_counter_clockwise();
        }
        if (action_pressed(InputAction::CameraRotateRight)) {
            rotate_clockwise();
        }

        // Zoom: scroll wheel, or a trackpad pinch
        float wheel = input::get_mouse_wheel_move();
        if (wheel != 0) {
            zoom(wheel);
        }
        float pinch = input::get_pinch_delta();
        if (pinch != 0.f) {
            pinch_zoom(pinch);
        }

        // Pan: WASD or arrow keys
        float move_x = 0.0f;
        float move_z = 0.0f;

        if (action_down(InputAction::CameraForward)) {
            move_z -= pan_speed * dt;
        }
        if (action_down(InputAction::CameraBack)) {
            move_z += pan_speed * dt;
        }
        if (action_down(InputAction::CameraLeft)) {
            move_x -= pan_speed * dt;
        }
        if (action_down(InputAction::CameraRight)) {
            move_x += pan_speed * dt;
        }

        if (move_x != 0.0f || move_z != 0.0f) {
            pan(move_x, move_z);
        }
    }

    afterhours::Camera3D* get_ptr() { return &camera; }

    // Convert screen position to grid coordinates.
    // Projects 3 reference grid points to screen via GetWorldToScreen,
    // then inverts the 2x2 affine transform. Avoids GetMouseRay/unproject
    // issues with orthographic cameras and Retina scaling.
    std::optional<std::pair<int, int>> screen_to_grid(float screen_x,
                                                      float screen_y) const {
        if (iso_mode) {
            // Invert the iso transform, then pick the tile whose diamond
            // contains the point: the nearest tile center under the L1
            // (|dx|+|dz|) metric. Rounding each axis separately would pick
            // square cells and mis-pick near diamond corners, so test the
            // four surrounding integer tiles instead.
            vec2 w = iso_screen_to_world(screen_x, screen_y);
            int fx = static_cast<int>(std::floor(w.x));
            int fz = static_cast<int>(std::floor(w.y));
            int best_x = fx, best_z = fz;
            float best_d = 1e9f;
            for (int dz = 0; dz <= 1; dz++) {
                for (int dx = 0; dx <= 1; dx++) {
                    int cx = fx + dx, cz = fz + dz;
                    float d = std::abs(w.x - cx) + std::abs(w.y - cz);
                    if (d < best_d) {
                        best_d = d;
                        best_x = cx;
                        best_z = cz;
                    }
                }
            }
            return std::make_pair(best_x, best_z);
        }
        // Project 3 grid reference points to screen space
        vec2 s00 = get_world_to_screen(vec3{0, 0, 0}, camera);
        vec2 s10 = get_world_to_screen(vec3{TILESIZE, 0, 0}, camera);
        vec2 s01 = get_world_to_screen(vec3{0, 0, TILESIZE}, camera);

        // Affine: screen = A * grid_pos + s00
        float a = s10.x - s00.x;  // screen_x change per grid_x step
        float b = s01.x - s00.x;  // screen_x change per grid_z step
        float d = s10.y - s00.y;  // screen_y change per grid_x step
        float e = s01.y - s00.y;  // screen_y change per grid_z step

        // Invert the 2x2 affine transform
        float det = a * e - b * d;
        if (std::abs(det) < 0.0001f) return std::nullopt;

        float sx = screen_x - s00.x;
        float sy = screen_y - s00.y;

        float gx = (e * sx - b * sy) / det;
        float gz = (a * sy - d * sx) / det;

        return std::make_pair((int) std::round(gx), (int) std::round(gz));
    }
};
