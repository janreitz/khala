#include "utility.h"

#include <string>

#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

namespace ui
{

struct UserInput {
    bool input_buffer_changed = false;
    bool selected_action_index_changed = false;
    bool exit_requested = false;
    bool action_requested = false;
};

UserInput process_input_events(Display* display, std::string& input_buffer, size_t& selected_action_index, size_t max_action_index);

struct Action {
    std::string title;
    std::string description;
};

void draw(Display *display, Window window, int width, int height,
          int input_height, const std::string &input_buffer, int action_height,
          const std::vector<Action> &actions, size_t selected_index);
} // namespace ui