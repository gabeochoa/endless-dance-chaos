#pragma once

#ifdef AFTER_HOURS_ENABLE_MCP

// Must include rl.h first - it sets up AFTER_HOURS_REPLACE_LOGGING
// and includes log.h before afterhours
#include "rl.h"

#include <afterhours/src/plugins/input_system.h>
#include <afterhours/src/plugins/mcp_server.h>

#include <set>
#include <sstream>

#include "game.h"

namespace mcp_integration {

namespace detail {
inline bool enabled = false;
inline std::set<int> keys_down;
inline std::set<int> keys_pressed_this_frame;
inline std::set<int> keys_released_this_frame;
inline raylib::Vector2 mouse_position = {0, 0};
inline bool mouse_clicked = false;
inline int mouse_button_clicked = 0;

// Store render texture reference for screenshots
inline raylib::RenderTexture2D* screenshot_texture = nullptr;

inline std::vector<uint8_t> capture_screenshot() {
    if (!screenshot_texture) {
        // Fallback: capture the whole screen
        raylib::Image img = raylib::LoadImageFromScreen();
        int file_size = 0;
        unsigned char* png_data =
            raylib::ExportImageToMemory(img, ".png", &file_size);

        std::vector<uint8_t> result;
        if (png_data && file_size > 0) {
            result.assign(png_data, png_data + file_size);
            raylib::MemFree(png_data);
        }

        raylib::UnloadImage(img);
        return result;
    }

    raylib::Image img =
        raylib::LoadImageFromTexture(screenshot_texture->texture);
    if (img.data == nullptr) {
        return {};  // Failed to load from texture
    }
    raylib::ImageFlipVertical(&img);

    int file_size = 0;
    unsigned char* png_data =
        raylib::ExportImageToMemory(img, ".png", &file_size);

    std::vector<uint8_t> result;
    if (png_data && file_size > 0) {
        result.assign(png_data, png_data + file_size);
        raylib::MemFree(png_data);
    }

    raylib::UnloadImage(img);
    return result;
}

inline std::pair<int, int> get_screen_size() {
    return {DEFAULT_SCREEN_WIDTH, DEFAULT_SCREEN_HEIGHT};
}

inline std::string dump_ui_tree() {
    std::ostringstream ss;
    ss << "UI Tree Dump:\n";
    ss << "  (No UI components registered yet)\n";
    return ss.str();
}

inline afterhours::mcp::MCPConfig create_config() {
    afterhours::mcp::MCPConfig config;

    config.get_screen_size = get_screen_size;
    config.capture_screenshot = capture_screenshot;
    config.dump_ui_tree = dump_ui_tree;

    config.mouse_move = [](int x, int y) {
        mouse_position = {static_cast<float>(x), static_cast<float>(y)};
    };

    config.mouse_click = [](int x, int y, int button) {
        mouse_position = {static_cast<float>(x), static_cast<float>(y)};
        mouse_clicked = true;
        mouse_button_clicked = button;
    };

    config.key_down = [](int keycode) {
        if (keys_down.count(keycode) == 0) {
            keys_pressed_this_frame.insert(keycode);
        }
        keys_down.insert(keycode);
    };

    config.key_up = [](int keycode) {
        keys_down.erase(keycode);
        keys_released_this_frame.insert(keycode);
    };

    return config;
}

// System to inject MCP inputs into the game
struct InjectInputSystem {
    void update(float dt) {
        (void) dt;
        if (!enabled) return;

        // For now, just track keys - will integrate with input system later
    }
};
}  // namespace detail

inline void set_screenshot_texture(raylib::RenderTexture2D* rt) {
    detail::screenshot_texture = rt;
}

inline void init() {
    detail::enabled = true;
    afterhours::mcp::init(detail::create_config());
    log_info("MCP server initialized");
}

inline void update() {
    if (!detail::enabled) return;
    afterhours::mcp::update();
}

inline void clear_frame_state() {
    if (!detail::enabled) return;
    detail::keys_pressed_this_frame.clear();
    detail::keys_released_this_frame.clear();
    detail::mouse_clicked = false;
}

inline void shutdown() {
    if (!detail::enabled) return;
    afterhours::mcp::shutdown();
    detail::enabled = false;
    log_info("MCP server shutdown");
}

inline bool exit_requested() {
    if (!detail::enabled) return false;
    return afterhours::mcp::exit_requested();
}

inline bool is_enabled() { return detail::enabled; }

// Check if a key is currently held down (from MCP)
inline bool is_key_down(int keycode) {
    return detail::keys_down.count(keycode) > 0;
}

// Check if a key was just pressed this frame (from MCP)
inline bool is_key_pressed(int keycode) {
    return detail::keys_pressed_this_frame.count(keycode) > 0;
}

// Get current mouse position (from MCP)
inline raylib::Vector2 get_mouse_position() { return detail::mouse_position; }

// Check if mouse was clicked this frame (from MCP)
inline bool is_mouse_clicked() { return detail::mouse_clicked; }

}  // namespace mcp_integration

#else

#include "rl.h"  // Need raylib::Vector2 for stub

// Stub implementation when MCP is disabled
namespace mcp_integration {
inline void set_screenshot_texture(void*) {}
inline void init() {}
inline void update() {}
inline void clear_frame_state() {}
inline void shutdown() {}
inline bool exit_requested() { return false; }
inline bool is_enabled() { return false; }
inline bool is_key_down(int) { return false; }
inline bool is_key_pressed(int) { return false; }
inline raylib::Vector2 get_mouse_position() { return {0, 0}; }
inline bool is_mouse_clicked() { return false; }
}  // namespace mcp_integration

#endif
