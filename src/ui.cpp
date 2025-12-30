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

enum class Corner : uint8_t {
    NoCorners        = 0,
    TopLeft     = 1 << 0,
    TopRight    = 1 << 1,
    BottomRight = 1 << 2,
    BottomLeft  = 1 << 3,
    All         = TopLeft | TopRight | BottomRight | BottomLeft
};

// Bitwise operators for Corner enum
constexpr Corner operator|(Corner a, Corner b) {
    return static_cast<Corner>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

constexpr bool operator&(Corner a, Corner b) {
    return static_cast<uint8_t>(a) & static_cast<uint8_t>(b);
}

static void draw_rounded_rect(cairo_t* cr, double x, double y, double width, double height,
                               double radius, Corner corners) {
    const double degrees = G_PI / 180.0;

    cairo_new_sub_path(cr);

    // Top-right corner
    if (corners & Corner::TopRight) {
        cairo_arc(cr, x + width - radius, y + radius, radius, -90 * degrees, 0 * degrees);
    } else {
        cairo_move_to(cr, x + width, y);
    }

    // Bottom-right corner
    if (corners & Corner::BottomRight) {
        cairo_arc(cr, x + width - radius, y + height - radius, radius, 0 * degrees, 90 * degrees);
    } else {
        cairo_line_to(cr, x + width, y + height);
    }

    // Bottom-left corner
    if (corners & Corner::BottomLeft) {
        cairo_arc(cr, x + radius, y + height - radius, radius, 90 * degrees, 180 * degrees);
    } else {
        cairo_line_to(cr, x, y + height);
    }

    // Top-left corner
    if (corners & Corner::TopLeft) {
        cairo_arc(cr, x + radius, y + radius, radius, 180 * degrees, 270 * degrees);
    } else {
        cairo_line_to(cr, x, y);
    }

    cairo_close_path(cr);
}

UserInput process_input_events(Display* display,  std::string& input_buffer, size_t& selected_action_index, size_t max_action_index)
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
                    state.selected_action_index_changed = true;
                }
                printf("Selected index: %ld\n", selected_action_index);
            } else if (keysym == XK_Down) {
                // Move selection down
                if (selected_action_index < max_action_index) {
                    selected_action_index++;
                    state.selected_action_index_changed = true;
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
          const std::vector<Item> &actions, size_t selected_action_index)
{
    // Get the window's visual (which should be ARGB for transparency)
    XWindowAttributes window_attrs;
    XGetWindowAttributes(display, window, &window_attrs);
    Visual *visual = window_attrs.visual;

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

    // Clear everything with transparent background
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    // Draw entire window background with rounded corners
    const double corner_radius = 8.0;
    draw_rounded_rect(cr, 0, 0, width, height, corner_radius, Corner::All);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_fill(cr);

    // Draw inset rounded rectangle for search input area
    const double inset = 4.0;
    const double input_corner_radius = 4.0;
    draw_rounded_rect(cr, inset, inset, width - 2 * inset, input_height - inset,
                      input_corner_radius, Corner::All);
    cairo_set_source_rgb(cr, 0.5, 0.5, 0.5); // Light gray background
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.5, 0.5, 0.5); // Light gray background
    cairo_stroke(cr);

    // Set font for launcher
    PangoFontDescription *font_desc =
        pango_font_description_from_string("Sans 12");
    const defer cleanup_font(
        [font_desc]() noexcept { pango_font_description_free(font_desc); });
    pango_layout_set_font_description(layout, font_desc);

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

        // Draw description to the right of the title in subtle grey
        if (!actions.at(i).description.empty()) {
            // Get the width of the title text
            int title_width;
            int title_height;
            pango_layout_get_size(layout, &title_width, &title_height);

            // Set subtle grey color for description
            if (i == selected_action_index) {
                cairo_set_source_rgb(cr, 0.85, 0.85, 0.85);  // Light grey on blue background
            } else {
                cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);     // Medium grey on white background
            }

            // Draw description with some spacing after the title
            cairo_move_to(cr, 15 + (title_width / PANGO_SCALE) + 10, y_pos + 8);
            pango_layout_set_text(layout, actions.at(i).description.c_str(), -1);
            pango_cairo_show_layout(cr, layout);
        }

        // Reset font for next iteration
        pango_layout_set_font_description(layout, font_desc);
    }

    // Flush to display
    cairo_surface_flush(surface);
}

} // namespace ui