#include "ui.h"
#include "utility.h"

#include <X11/X.h>
#include <X11/Xlib.h>

#include <cairo-xlib.h>
#include <cairo.h>

#include "pango/pango-font.h"
#include "pango/pango-layout.h"
#include "pango/pango-types.h"
#include <pango/pangocairo.h>

#include <string>

namespace ui
{

UserInput process_input_events(Display* display,  std::string& input_buffer, size_t selected_action_index, size_t max_action_index)
{
    XEvent event;

    UserInput state;

    while (XPending(display) > 0) {
        XNextEvent(display, &event);

        if (event.type == Expose) {
            if (event.xexpose.count == 0) {
            }
        } else if (event.type == KeyPress) {
            const KeySym keysym = XLookupKeysym(&event.xkey, 0);

            if (keysym == XK_Escape) {
                state.exit_requested = true;
            } else if (keysym == XK_Up) {
                // Move selection up
                if (selected_action_index > 0) {
                    selected_action_index--;
                }
                printf("Selected index: %ld\n", selected_action_index);
            } else if (keysym == XK_Down) {
                // Move selection down
                if (selected_action_index < max_action_index) {
                    selected_action_index++;
                }
                printf("Selected index: %ld\n", selected_action_index);
            } else if (keysym == XK_Return) {
                    state.action_requested = true; // Exit for now
            } else if (keysym == XK_BackSpace) {
                // Handle backspace
                if (!input_buffer.empty()) {
                    input_buffer.pop_back();
                    state.input_buffer_changed = true;
                    selected_action_index = 0; // Reset selection when search changes
                }
            } else {
                // Handle regular character input
                std::array<char, 32> char_buffer;
                const int len =
                    XLookupString(&event.xkey, char_buffer.data(),
                                  char_buffer.size(), nullptr, nullptr);
                if (len > 0) {
                    char_buffer[len] = '\0';
                    // Only add printable characters
                    for (int i = 0; i < len; ++i) {
                        if (char_buffer[i] >= 32 && char_buffer[i] < 127) {
                            input_buffer += char_buffer[i];
                            state.input_buffer_changed = true;
                            selected_action_index =
                                0; // Reset selection when search changes
                        }
                    }
                }
            }
            break;
        }
    }
    return state;
}

void draw(Display *display, Window window, int width, int height,
          int input_height, const std::string &input_buffer, int action_height,
          const std::vector<Action> &actions, size_t selected_action_index)
{
    // Get the default visual
    const int screen = DefaultScreen(display);
    Visual *visual = DefaultVisual(display, screen);

    // Create Cairo surface for X11 window
    cairo_surface_t *surface =
        cairo_xlib_surface_create(display, window, visual, width, height);
    const defer cleanup_surface(
        [surface]() noexcept { cairo_surface_destroy(surface); });

    // Create Cairo context
    cairo_t *cr = cairo_create(surface);
    const defer cleanup_cr([cr]() noexcept { cairo_destroy(cr); });

    // Create Pango layout for text rendering
    PangoLayout *layout = pango_cairo_create_layout(cr);
    const defer cleanup_layout([layout]() noexcept { g_object_unref(layout); });

    // Clear background with white
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    // Set font for launcher
    PangoFontDescription *font_desc =
        pango_font_description_from_string("Sans 12");
    const defer cleanup_font(
        [font_desc]() noexcept { pango_font_description_free(font_desc); });
    pango_layout_set_font_description(layout, font_desc);

    // Draw search input area
    cairo_set_source_rgb(cr, 0.95, 0.95, 0.95); // Light gray background
    cairo_rectangle(cr, 0, 0, width, input_height);
    cairo_fill(cr);

    // Draw search prompt and buffer
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_move_to(cr, 10, 15);

    std::string display_text = "> " + input_buffer;
    if (input_buffer.empty()) {
        display_text += "(type to search...)";
    }

    pango_layout_set_text(layout, display_text.c_str(), -1);
    pango_layout_set_attributes(layout, nullptr);
    pango_cairo_show_layout(cr, layout);

    // Draw cursor after text if there's content
    if (!input_buffer.empty()) {
        int text_width;
        int text_height;
        pango_layout_get_size(layout, &text_width, &text_height);

        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_move_to(cr, 10 + (text_width / PANGO_SCALE), 15);
        cairo_line_to(cr, 10 + (text_width / PANGO_SCALE),
                      15 + (text_height / PANGO_SCALE));
        cairo_stroke(cr);
    }

    // Draw separator line
    cairo_set_source_rgb(cr, 0.7, 0.7, 0.7);
    cairo_move_to(cr, 0, input_height);
    cairo_line_to(cr, width, input_height);
    cairo_stroke(cr);

    // Draw dropdown options
    for (size_t i = 0; i < actions.size(); ++i) {
        const int y_pos = input_height + (i * action_height);

        // Draw selection highlight
        if (i == selected_action_index) {
            cairo_set_source_rgb(cr, 0.3, 0.6, 1.0); // Blue highlight
            cairo_rectangle(cr, 0, y_pos, width, action_height);
            cairo_fill(cr);
        }

        // Set text color (white on selected, black on normal)
        if (i == selected_action_index) {
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        } else {
            cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        }

        // Draw filename (main text)
        cairo_move_to(cr, 15, y_pos + 8);
        pango_layout_set_text(layout, actions.at(i).title.c_str(), -1);
        pango_cairo_show_layout(cr, layout);

        // Reset font for next iteration
        pango_layout_set_font_description(layout, font_desc);
    }

    // Flush to display
    cairo_surface_flush(surface);
}

} // namespace ui