#include "utility.h"

#include <string>

#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

namespace ui
{

void draw(Display *display, Window window, int width, int height,
          int input_height, int option_height, int max_visible_options,
          const std::string &query_buffer, const PackedStrings &results,
          int selected_index);
} // namespace ui