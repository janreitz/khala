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

struct WindowDimension {
    unsigned int height;
    unsigned int width;
};

enum class KeyCode {
    None,
    Escape,
    Return,
    BackSpace,
    Delete,
    Tab,
    Space,
    Up,
    Down,
    Left,
    Right,
    Home,
    End,
    // Letter keys A-Z
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    // Number keys 0-9
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    // Function keys
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    // For printable characters not covered above
    Character,
};

// Modifier flags - can be combined with |
enum class KeyModifier : uint8_t {
    None  = 0,
    Ctrl  = 1 << 0,
    Alt   = 1 << 1,
    Shift = 1 << 2,
    Super = 1 << 3,  // Win key on Windows, Meta on Linux
};

// Bitwise operators for KeyModifier flags
inline KeyModifier operator|(KeyModifier a, KeyModifier b) {
    return static_cast<KeyModifier>(
        static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline KeyModifier operator&(KeyModifier a, KeyModifier b) {
    return static_cast<KeyModifier>(
        static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

inline KeyModifier& operator|=(KeyModifier& a, KeyModifier b) {
    return a = a | b;
}

inline bool has_modifier(KeyModifier flags, KeyModifier test) {
    return static_cast<uint8_t>(flags & test) != 0;
}

struct KeyboardEvent {
    KeyCode key = KeyCode::None;
    KeyModifier modifiers = KeyModifier::None;
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

struct MouseScrollEvent {
    enum class Direction {
        Up,
        Down
    };

    Direction direction;
    WindowCoord position;
};

// Global hotkey was pressed (for background mode toggle)
struct HotkeyEvent {};

using UserInputEvent = std::variant<
    KeyboardEvent,
    MousePositionEvent,
    MouseButtonEvent,
    CursorEnterEvent,
    CursorLeaveEvent,
    MouseScrollEvent,
    HotkeyEvent
>;

} // namespace ui
