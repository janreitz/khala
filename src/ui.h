#include "actions.h"
#include "utility.h"

#include <string>
#include <variant>

#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

namespace ui
{

struct Item {
    std::string title;
    std::string description;
    std::vector<Action> actions;
};

struct State {
    std::string input_buffer;

    // Results
    std::vector<Item> items;
    size_t selected_item_index = 0;

    // Context menu
    bool context_menu_open = false;
    size_t selected_action_index = 0;
};

enum class Event {
    NoEvent,
    InputChanged,
    SelectionChanged,
    ActionRequested,
    ContextMenuToggled,
    ExitRequested,
};

Event process_input_events(Display *display, State &state);

void draw(Display *display, Window window, const State &state, int width,
          int height, int input_height, int action_height);

} // namespace ui