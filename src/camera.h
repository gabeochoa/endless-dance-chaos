
#pragma once

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
        if (raylib::IsKeyPressed(raylib::KEY_Q)) {
            rotate_counter_clockwise();
        }
        if (raylib::IsKeyPressed(raylib::KEY_E)) {
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

        if (raylib::IsKeyDown(raylib::KEY_W) ||
            raylib::IsKeyDown(raylib::KEY_UP)) {
            move_z -= pan_speed * dt;
        }
        if (raylib::IsKeyDown(raylib::KEY_S) ||
            raylib::IsKeyDown(raylib::KEY_DOWN)) {
            move_z += pan_speed * dt;
        }
        if (raylib::IsKeyDown(raylib::KEY_A) ||
            raylib::IsKeyDown(raylib::KEY_LEFT)) {
            move_x -= pan_speed * dt;
        }
        if (raylib::IsKeyDown(raylib::KEY_D) ||
            raylib::IsKeyDown(raylib::KEY_RIGHT)) {
            move_x += pan_speed * dt;
        }

        if (move_x != 0.0f || move_z != 0.0f) {
            pan(move_x, move_z);
        }
    }

    raylib::Camera3D* get_ptr() { return &camera; }
};
