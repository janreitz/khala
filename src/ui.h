#include "actions.h"
#include "config.h"
#include "utility.h"

#include <string>
#include <variant>

#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

namespace ui
{


struct XWindow {
    Display* display = nullptr;
    ::Window window = 0;
    Colormap colormap = 0;
    int width = 0;
    int height = 0;
    int screen_height = 0;
    
    XWindow(const Config& config);
    ~XWindow();
    
    // Non-copyable
    XWindow(const XWindow&) = delete;
    XWindow& operator=(const XWindow&) = delete;
};

struct Item {
    std::string title;
    std::string description;
    std::vector<Action> actions;
};

struct State {
    std::string input_buffer;
    size_t cursor_position = 0;

    // Results
    std::vector<Item> items;
    size_t selected_item_index = 0;

    // Context menu
    bool context_menu_open = false;
    size_t selected_action_index = 0;

    Item get_selected_item() const;
    Action get_selected_action() const;
};

enum class Event {
    NoEvent,
    InputChanged,
    SelectionChanged,
    CursorPositionChanged,
    ActionRequested,
    ContextMenuToggled,
    ExitRequested,
};

Event process_input_events(Display *display, State &state);

void draw(XWindow& window, const Config& config, const State &state);

} // namespace ui