
#pragma once

#include "game.h"
#include "input_mapping.h"
#include "log.h"
#include "rl.h"

// RCT-style isometric camera with 90-degree rotation support
// Uses orthographic projection for classic isometric look
struct IsometricCamera {
    raylib::Camera3D camera;

    // Camera distance and angle settings
    float distance = 30.0f;
    float min_distance = 5.0f;
    float max_distance = 50.0f;
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
        camera.projection = raylib::CAMERA_ORTHOGRAPHIC;
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

    // Zoom in/out
    void zoom(float delta) {
        distance -= delta * scroll_sensitivity;
        distance = std::clamp(distance, min_distance, max_distance);

        // Also adjust orthographic "zoom" (fovy for ortho is like zoom level)
        camera.fovy = distance;
        update_camera_position();
    }

    // Pan the camera target
    void pan(float dx, float dz) {
        // Adjust pan direction based on current rotation
        float cos_yaw = cosf(yaw);
        float sin_yaw = sinf(yaw);

        target.x += dx * cos_yaw + dz * sin_yaw;
        target.z += -dx * sin_yaw + dz * cos_yaw;
        update_camera_position();
    }

    // Handle input for camera controls
    void handle_input(float dt) {
        // Rotation: Q and E keys
        if (action_pressed(InputAction::CameraRotateLeft)) {
            rotate_counter_clockwise();
        }
        if (action_pressed(InputAction::CameraRotateRight)) {
            rotate_clockwise();
        }

        // Zoom: scroll wheel
        float wheel = raylib::GetMouseWheelMove();
        if (wheel != 0) {
            zoom(wheel);
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

    raylib::Camera3D* get_ptr() { return &camera; }

    // Convert screen position to grid coordinates.
    // Projects 3 reference grid points to screen via GetWorldToScreen,
    // then inverts the 2x2 affine transform. Avoids GetMouseRay/unproject
    // issues with orthographic cameras and Retina scaling.
    std::optional<std::pair<int, int>> screen_to_grid(float screen_x,
                                                      float screen_y) const {
        // Project 3 grid reference points to screen space
        raylib::Vector2 s00 = raylib::GetWorldToScreen({0, 0, 0}, camera);
        raylib::Vector2 s10 =
            raylib::GetWorldToScreen({TILESIZE, 0, 0}, camera);
        raylib::Vector2 s01 =
            raylib::GetWorldToScreen({0, 0, TILESIZE}, camera);

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
