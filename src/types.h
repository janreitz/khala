#pragma once

#include <optional>
#include <variant>

namespace ui {

struct RelScreenCoord {
    double x;
    double y;
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

using UserInputEvent = std::variant<KeyboardEvent>;

} // namespace ui
