#pragma once

#include "rl.h"

enum class InputAction {
    None,
    // Camera
    CameraForward,
    CameraBack,
    CameraLeft,
    CameraRight,
    CameraRotateLeft,
    CameraRotateRight,
    // Build tools
    ToolPath,
    ToolBathroom,
    ToolFood,
    ToolStage,
    // Actions
    PlaceOrConfirm,
    Cancel,
    ToggleDemolish,
    // Data layer
    ToggleDataLayer,
    FilterBathroom,
    FilterFood,
    FilterStage,
    // UI
    WidgetRight,
    WidgetLeft,
    WidgetNext,
    WidgetPress,
    WidgetMod,
    WidgetBack,
    MenuBack,
    PauseButton,
    ToggleUIDebug,
    ToggleUILayoutDebug,
    // Build tool cycling
    PrevTool,
    NextTool,
    ToolFence,
    ToolGate,
    ToolDemolish,
    Tool5,  // Bathroom
    Tool6,  // Food
    Tool7,  // MedTent
    Tool8,  // Demolish
    // Game state
    Restart,
    TogglePause,
};

inline int to_int(InputAction action) { return static_cast<int>(action); }

inline InputAction from_int(int value) {
    return static_cast<InputAction>(value);
}

inline bool action_matches(int action, InputAction expected) {
    return from_int(action) == expected;
}

using afterhours::input;

// Helper to check if an action was pressed this frame
inline bool action_pressed(InputAction action) {
    auto collector = afterhours::input::get_input_collector();
    if (!collector.valid()) return false;
    for (const auto& input : collector.inputs_pressed()) {
        if (action_matches(input.action, action)) return true;
    }
    return false;
}

// Helper to check if an action is held down
inline bool action_down(InputAction action) {
    auto collector = afterhours::input::get_input_collector();
    if (!collector.valid()) return false;
    for (const auto& input : collector.inputs()) {
        if (action_matches(input.action, action)) return true;
    }
    return false;
}

inline auto get_mapping() {
    std::map<int, input::ValidInputs> mapping;

    // Camera movement - WASD + arrows
    mapping[to_int(InputAction::CameraForward)] = {
        raylib::KEY_W,
        raylib::KEY_UP,
    };
    mapping[to_int(InputAction::CameraBack)] = {
        raylib::KEY_S,
        raylib::KEY_DOWN,
    };
    mapping[to_int(InputAction::CameraLeft)] = {
        raylib::KEY_A,
        raylib::KEY_LEFT,
    };
    mapping[to_int(InputAction::CameraRight)] = {
        raylib::KEY_D,
        raylib::KEY_RIGHT,
    };
    mapping[to_int(InputAction::CameraRotateLeft)] = {
        raylib::KEY_Q,
    };
    mapping[to_int(InputAction::CameraRotateRight)] = {
        raylib::KEY_E,
    };

    // Build tools - number keys
    mapping[to_int(InputAction::ToolPath)] = {
        raylib::KEY_ONE,
    };
    mapping[to_int(InputAction::ToolBathroom)] = {
        raylib::KEY_TWO,
    };
    mapping[to_int(InputAction::ToolFood)] = {
        raylib::KEY_THREE,
    };
    mapping[to_int(InputAction::ToolStage)] = {
        raylib::KEY_FOUR,
    };

    // Actions
    mapping[to_int(InputAction::PlaceOrConfirm)] = {
        raylib::KEY_ENTER,
        raylib::GAMEPAD_BUTTON_RIGHT_FACE_DOWN,
    };
    mapping[to_int(InputAction::Cancel)] = {
        raylib::KEY_ESCAPE,
    };
    mapping[to_int(InputAction::ToggleDemolish)] = {
        raylib::KEY_X,
    };

    // Data layer
    mapping[to_int(InputAction::ToggleDataLayer)] = {
        raylib::KEY_TAB,
    };
    mapping[to_int(InputAction::FilterBathroom)] = {
        raylib::KEY_ONE,
    };
    mapping[to_int(InputAction::FilterFood)] = {
        raylib::KEY_TWO,
    };
    mapping[to_int(InputAction::FilterStage)] = {
        raylib::KEY_THREE,
    };

    // Widget navigation
    mapping[to_int(InputAction::WidgetLeft)] = {
        raylib::KEY_LEFT,
        raylib::GAMEPAD_BUTTON_LEFT_FACE_LEFT,
    };
    mapping[to_int(InputAction::WidgetRight)] = {
        raylib::KEY_RIGHT,
        raylib::GAMEPAD_BUTTON_LEFT_FACE_RIGHT,
    };
    mapping[to_int(InputAction::WidgetBack)] = {
        raylib::GAMEPAD_BUTTON_LEFT_FACE_UP,
        raylib::KEY_UP,
    };
    mapping[to_int(InputAction::WidgetNext)] = {
        raylib::KEY_TAB,
        raylib::GAMEPAD_BUTTON_LEFT_FACE_DOWN,
        raylib::KEY_DOWN,
    };
    mapping[to_int(InputAction::WidgetPress)] = {
        raylib::KEY_ENTER,
        raylib::GAMEPAD_BUTTON_RIGHT_FACE_DOWN,
    };
    mapping[to_int(InputAction::WidgetMod)] = {
        raylib::KEY_LEFT_SHIFT,
    };
    mapping[to_int(InputAction::MenuBack)] = {
        raylib::KEY_ESCAPE,
    };
    mapping[to_int(InputAction::PauseButton)] = {
        raylib::KEY_ESCAPE,
        raylib::GAMEPAD_BUTTON_MIDDLE_RIGHT,
    };
    mapping[to_int(InputAction::ToggleUIDebug)] = {
        raylib::KEY_GRAVE,
    };
    mapping[to_int(InputAction::ToggleUILayoutDebug)] = {
        raylib::KEY_EQUAL,
    };

    // Build tool cycling
    mapping[to_int(InputAction::PrevTool)] = {
        raylib::KEY_LEFT_BRACKET,
    };
    mapping[to_int(InputAction::NextTool)] = {
        raylib::KEY_RIGHT_BRACKET,
    };
    mapping[to_int(InputAction::ToolFence)] = {
        raylib::KEY_TWO,
    };
    mapping[to_int(InputAction::ToolGate)] = {
        raylib::KEY_THREE,
    };
    mapping[to_int(InputAction::Tool5)] = {
        raylib::KEY_FIVE,
    };
    mapping[to_int(InputAction::Tool6)] = {
        raylib::KEY_SIX,
    };
    mapping[to_int(InputAction::Tool7)] = {
        raylib::KEY_SEVEN,
    };
    mapping[to_int(InputAction::Tool8)] = {
        raylib::KEY_EIGHT,
    };

    // Game state
    mapping[to_int(InputAction::Restart)] = {
        raylib::KEY_SPACE,
    };
    mapping[to_int(InputAction::TogglePause)] = {
        raylib::KEY_SPACE,
    };

    return mapping;
}
