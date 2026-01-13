#pragma once

#include <optional>
#include <variant>

namespace ui {

struct RelScreenCoord {
    double x;
    double y;
};

struct WindowCoord {
    int x;
    int y;
};

enum class KeyCode {
    Escape,
    Return,
    BackSpace,
    Delete,
    Tab,
    Up,
    Down,
    Left,
    Right,
    Home,
    End,
    Character, // For printable characters
};

enum class KeyModifier {
    Ctrl,
    Alt,
    Shift,
};

struct KeyboardEvent {
    KeyCode key;
    std::optional<KeyModifier> modifier;
    std::optional<char> character; // For KeyCode::Character events
};

struct MousePositionEvent {
    WindowCoord position;
};

struct MouseButtonEvent {
    enum class Button {
        Left,
        Right,
        Middle
    };

    Button button;
    bool pressed;  // true = press, false = release
    WindowCoord position;
};

struct CursorEnterEvent {
    WindowCoord position;
};

struct CursorLeaveEvent {
};

using UserInputEvent = std::variant<
    KeyboardEvent,
    MousePositionEvent,
    MouseButtonEvent,
    CursorEnterEvent,
    CursorLeaveEvent
>;

} // namespace ui
