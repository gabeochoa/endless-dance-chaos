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
    // Save/Load
    QuickSave,
    QuickLoad,
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
        KEY_W,
        KEY_UP,
    };
    mapping[to_int(InputAction::CameraBack)] = {
        KEY_S,
        KEY_DOWN,
    };
    mapping[to_int(InputAction::CameraLeft)] = {
        KEY_A,
        KEY_LEFT,
    };
    mapping[to_int(InputAction::CameraRight)] = {
        KEY_D,
        KEY_RIGHT,
    };
    mapping[to_int(InputAction::CameraRotateLeft)] = {
        KEY_Q,
    };
    mapping[to_int(InputAction::CameraRotateRight)] = {
        KEY_E,
    };

    // Build tools - number keys (1-8 for each tool in order)
    mapping[to_int(InputAction::ToolPath)] = {
        KEY_ONE,
    };
    mapping[to_int(InputAction::ToolFence)] = {
        KEY_TWO,
    };
    mapping[to_int(InputAction::ToolGate)] = {
        KEY_THREE,
    };
    mapping[to_int(InputAction::ToolStage)] = {
        KEY_FOUR,
    };

    // Actions
    mapping[to_int(InputAction::PlaceOrConfirm)] = {
        KEY_ENTER,
        GAMEPAD_BUTTON_RIGHT_FACE_DOWN,
    };
    mapping[to_int(InputAction::Cancel)] = {
        KEY_ESCAPE,
    };
    mapping[to_int(InputAction::ToggleDemolish)] = {
        KEY_X,
    };

    // Data layer (TAB for overlay, filters unused for now to avoid key
    // conflicts)
    mapping[to_int(InputAction::ToggleDataLayer)] = {
        KEY_TAB,
    };

    // Widget navigation
    mapping[to_int(InputAction::WidgetLeft)] = {
        KEY_LEFT,
        GAMEPAD_BUTTON_LEFT_FACE_LEFT,
    };
    mapping[to_int(InputAction::WidgetRight)] = {
        KEY_RIGHT,
        GAMEPAD_BUTTON_LEFT_FACE_RIGHT,
    };
    mapping[to_int(InputAction::WidgetBack)] = {
        GAMEPAD_BUTTON_LEFT_FACE_UP,
        KEY_UP,
    };
    mapping[to_int(InputAction::WidgetNext)] = {
        GAMEPAD_BUTTON_LEFT_FACE_DOWN,
        KEY_DOWN,
    };
    mapping[to_int(InputAction::WidgetPress)] = {
        KEY_ENTER,
        GAMEPAD_BUTTON_RIGHT_FACE_DOWN,
    };
    mapping[to_int(InputAction::WidgetMod)] = {
        KEY_LEFT_SHIFT,
    };
    mapping[to_int(InputAction::MenuBack)] = {
        KEY_ESCAPE,
    };
    mapping[to_int(InputAction::PauseButton)] = {
        KEY_ESCAPE,
        GAMEPAD_BUTTON_MIDDLE_RIGHT,
    };
    mapping[to_int(InputAction::ToggleUIDebug)] = {
        KEY_GRAVE,
    };
    mapping[to_int(InputAction::ToggleUILayoutDebug)] = {
        KEY_EQUAL,
    };

    // Build tool cycling
    mapping[to_int(InputAction::PrevTool)] = {
        KEY_LEFT_BRACKET,
    };
    mapping[to_int(InputAction::NextTool)] = {
        KEY_RIGHT_BRACKET,
    };
    mapping[to_int(InputAction::Tool5)] = {
        KEY_FIVE,
    };
    mapping[to_int(InputAction::Tool6)] = {
        KEY_SIX,
    };
    mapping[to_int(InputAction::Tool7)] = {
        KEY_SEVEN,
    };
    mapping[to_int(InputAction::Tool8)] = {
        KEY_EIGHT,
    };
    mapping[to_int(InputAction::QuickSave)] = {
        KEY_F5,
    };
    mapping[to_int(InputAction::QuickLoad)] = {
        KEY_F9,
    };

    // Game state
    mapping[to_int(InputAction::Restart)] = {
        KEY_SPACE,
    };
    mapping[to_int(InputAction::TogglePause)] = {
        KEY_SPACE,
    };

    return mapping;
}
