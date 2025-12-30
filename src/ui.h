#include "utility.h"

#include <string>

#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

namespace ui
{

struct Action {
    std::string title;
    std::string description;
};

void draw(Display *display, Window window, int width, int height,
          int input_height, const std::string &input_buffer, int action_height,
          const std::vector<Action> &actions, int selected_index);
} // namespace ui