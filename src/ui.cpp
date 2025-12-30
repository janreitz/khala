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

#include <algorithm>
#include <ranges>
#include <string>

namespace ui
{

Item State::get_selected_item() const { return items.at(selected_item_index); }

Action State::get_selected_action() const
{
    return get_selected_item().actions.at(selected_action_index);
}

enum class Corner : uint8_t {
    NoCorners = 0,
    TopLeft = 1 << 0,
    TopRight = 1 << 1,
    BottomRight = 1 << 2,
    BottomLeft = 1 << 3,
    All = TopLeft | TopRight | BottomRight | BottomLeft
};

// Bitwise operators for Corner enum
constexpr Corner operator|(Corner a, Corner b)
{
    return static_cast<Corner>(static_cast<uint8_t>(a) |
                               static_cast<uint8_t>(b));
}

constexpr bool operator&(Corner a, Corner b)
{
    return static_cast<uint8_t>(a) & static_cast<uint8_t>(b);
}

static void draw_rounded_rect(cairo_t *cr, double x, double y, double width,
                              double height, double radius, Corner corners)
{
    const double degrees = G_PI / 180.0;

    cairo_new_sub_path(cr);

    // Top-right corner
    if (corners & Corner::TopRight) {
        cairo_arc(cr, x + width - radius, y + radius, radius, -90 * degrees,
                  0 * degrees);
    } else {
        cairo_move_to(cr, x + width, y);
    }

    // Bottom-right corner
    if (corners & Corner::BottomRight) {
        cairo_arc(cr, x + width - radius, y + height - radius, radius,
                  0 * degrees, 90 * degrees);
    } else {
        cairo_line_to(cr, x + width, y + height);
    }

    // Bottom-left corner
    if (corners & Corner::BottomLeft) {
        cairo_arc(cr, x + radius, y + height - radius, radius, 90 * degrees,
                  180 * degrees);
    } else {
        cairo_line_to(cr, x, y + height);
    }

    // Top-left corner
    if (corners & Corner::TopLeft) {
        cairo_arc(cr, x + radius, y + radius, radius, 180 * degrees,
                  270 * degrees);
    } else {
        cairo_line_to(cr, x, y);
    }

    cairo_close_path(cr);
}

Event process_input_events(Display *display, State &state)
{
    XEvent event;

    Event out_event = Event::NoEvent;

    while (XPending(display) > 0) {
        XNextEvent(display, &event);

        if (event.type == Expose) {
            if (event.xexpose.count == 0) {
            }
        } else if (event.type == KeyPress) {
            const KeySym keysym = XLookupKeysym(&event.xkey, 0);

            if (keysym == XK_Escape) {
                out_event = Event::ExitRequested;
            } else if (keysym == XK_Up) {
                // Move selection up
                if (state.context_menu_open) {
                    if (state.selected_action_index > 0) {
                        state.selected_action_index--;
                        out_event = Event::SelectionChanged;
                    }
                } else {
                    if (state.selected_item_index > 0) {
                        state.selected_item_index--;
                        out_event = Event::SelectionChanged;
                    }
                }
            } else if (keysym == XK_Down) {
                // Move selection down
                if (state.context_menu_open) {
                    const size_t max_action_index =
                        state.get_selected_item().actions.size() - 1;
                    if (state.selected_action_index < max_action_index) {
                        state.selected_action_index++;
                        out_event = Event::SelectionChanged;
                    }
                } else {
                    if (state.selected_item_index < state.items.size() - 1) {
                        state.selected_item_index++;
                        out_event = Event::SelectionChanged;
                    }
                }
            } else if (keysym == XK_Tab) {
                // Open context menu
                if (!state.context_menu_open && !state.items.empty() &&
                    !state.get_selected_item().actions.empty()) {
                    state.context_menu_open = true;
                    state.selected_action_index = 0;
                    out_event = Event::ContextMenuToggled;
                }
            } else if (keysym == XK_Left) {
                if (state.context_menu_open) {
                    // Close context menu
                    state.context_menu_open = false;
                    out_event = Event::ContextMenuToggled;
                } else {
                    // Move cursor left
                    if (state.cursor_position > 0) {
                        state.cursor_position--;
                        out_event = Event::CursorPositionChanged;
                    }
                }
            } else if (keysym == XK_Right) {
                // Move cursor right (only when not in context menu)
                if (!state.context_menu_open && state.cursor_position < state.input_buffer.size()) {
                    state.cursor_position++;
                    out_event = Event::CursorPositionChanged;
                }
            } else if (keysym == XK_Home) {
                // Jump to beginning
                if (!state.context_menu_open) {
                    state.cursor_position = 0;
                    out_event = Event::CursorPositionChanged;
                }
            } else if (keysym == XK_End) {
                // Jump to end
                if (!state.context_menu_open) {
                    state.cursor_position = state.input_buffer.size();
                    out_event = Event::CursorPositionChanged;
                }
            } else if (keysym == XK_Return) {
                out_event = Event::ActionRequested;
            } else if (keysym == XK_BackSpace) {
                // Handle backspace at cursor position
                if (!state.input_buffer.empty() && state.cursor_position > 0) {
                    state.input_buffer.erase(state.cursor_position - 1, 1);
                    state.cursor_position--;
                    out_event = Event::InputChanged;
                }
            } else {
                // Handle regular character input at cursor position
                std::array<char, 32> char_buffer;
                const int len =
                    XLookupString(&event.xkey, char_buffer.data(),
                                  char_buffer.size(), nullptr, nullptr);
                if (len > 0) {
                    char_buffer[len] = '\0';
                    // Only add printable characters
                    for (int i = 0; i < len; ++i) {
                        if (char_buffer[i] >= 32 && char_buffer[i] < 127) {
                            state.input_buffer.insert(state.cursor_position, 1, char_buffer[i]);
                            state.cursor_position++;
                            out_event = Event::InputChanged;
                        }
                    }
                }
            }
            break;
        }
    }
    return out_event;
}

void draw(Display *display, Window window, const State &state, int width,
          int height, int input_height, int action_height)
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
    const double border_width = 3.0;

    // Fill entire window with input color (grey)
    draw_rounded_rect(cr, 0, 0, width, height, corner_radius, Corner::All);
    cairo_set_source_rgb(cr, 0.92, 0.92, 0.92);
    cairo_fill(cr);

    // Draw white background for dropdown area if there are items
    if (height > input_height) {
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        draw_rounded_rect(cr, 0, input_height, width, height - input_height,
                          corner_radius, Corner::BottomLeft | Corner::BottomRight);
        cairo_fill(cr);
    }

    // Draw white border around entire window
    draw_rounded_rect(cr, 0, 0, width, height, corner_radius, Corner::All);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_set_line_width(cr, border_width);
    cairo_stroke(cr);

    // Set font for launcher
    PangoFontDescription *font_desc =
        pango_font_description_from_string("Sans 12");
    const defer cleanup_font(
        [font_desc]() noexcept { pango_font_description_free(font_desc); });
    pango_layout_set_font_description(layout, font_desc);

    // Draw search prompt and buffer
    std::string display_text;
    if (state.context_menu_open) {
        // Show selected item title when in context menu
        display_text = state.get_selected_item().title + " › Actions";
    } else {
        display_text = state.input_buffer;
        if (state.input_buffer.empty()) {
            display_text = "Search files... (prefix > for utility actions)";
        }
    }

    pango_layout_set_text(layout, display_text.c_str(), -1);
    pango_layout_set_attributes(layout, nullptr);

    // Get text dimensions for vertical centering
    int text_width;
    int text_height;
    pango_layout_get_size(layout, &text_width, &text_height);
    const double text_y = (input_height - (text_height / PANGO_SCALE)) / 2.0;

    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_move_to(cr, 10, text_y);
    pango_cairo_show_layout(cr, layout);

    // Draw cursor at cursor position when not in context menu
    if (!state.context_menu_open) {
        // Get width of text up to cursor position
        const std::string text_before_cursor = state.input_buffer.substr(0, state.cursor_position);
        pango_layout_set_text(layout, text_before_cursor.c_str(), -1);
        int cursor_x_offset;
        int cursor_height;
        pango_layout_get_size(layout, &cursor_x_offset, &cursor_height);

        // Draw cursor line
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        const double cursor_x = 10 + (cursor_x_offset / PANGO_SCALE);
        cairo_move_to(cr, cursor_x, text_y);
        cairo_line_to(cr, cursor_x, text_y + (text_height / PANGO_SCALE));
        cairo_stroke(cr);

        // Restore original text for layout
        pango_layout_set_text(layout, display_text.c_str(), -1);
    }

    struct DropdownItem {
        std::string title;
        std::string description;
    };

    auto to_dropdown = [](const auto& x) {
        return DropdownItem{.title = x.title, .description = x.description};
    };

    auto [dropdown_items, selection_index] = [&]() -> std::pair<std::vector<DropdownItem>, size_t> {
        if (state.context_menu_open) {
            auto transformed = state.get_selected_item().actions | std::views::transform(to_dropdown);
            return {
                std::vector<DropdownItem>(transformed.begin(), transformed.end()),
                state.selected_action_index
            };
        } else {
            auto transformed = state.items | std::views::transform(to_dropdown);
            return {
                std::vector<DropdownItem>(transformed.begin(), transformed.end()),
                state.selected_item_index
            };
        }
    }();

    // Draw dropdown items
    for (size_t i = 0; i < dropdown_items.size(); ++i) {
        const int y_pos = input_height + (i * action_height);

        // Draw selection highlight
        if (i == selection_index) {
            cairo_set_source_rgb(cr, 0.3, 0.6, 1.0); // Blue highlight

            // Use rounded bottom corners if this is the last item
            const bool is_last_item = (i == dropdown_items.size() - 1);
            if (is_last_item) {
                draw_rounded_rect(cr, 0, y_pos, width, action_height, corner_radius,
                                  Corner::BottomLeft | Corner::BottomRight);
            } else {
                draw_rounded_rect(cr, 0, y_pos, width, action_height, 0, Corner::NoCorners);
            }
            cairo_fill(cr);
        }

        // Set text color (white on selected, black on normal)
        if (i == selection_index) {
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        } else {
            cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        }

        // Draw filename (main text)
        cairo_move_to(cr, 15, y_pos + 8);
        pango_layout_set_text(layout, dropdown_items.at(i).title.c_str(), -1);
        pango_cairo_show_layout(cr, layout);

        // Draw description to the right of the title in subtle grey
        if (!dropdown_items.at(i).description.empty()) {
            // Get the width of the title text
            int title_width;
            int title_height;
            pango_layout_get_size(layout, &title_width, &title_height);

            // Calculate available width for description
            const int spacing = 10;
            const int left_margin = 15;
            const int right_margin = 15;
            const int available_width = width - left_margin - (title_width / PANGO_SCALE) - spacing - right_margin;

            // Set subtle grey color for description
            if (i == selection_index) {
                cairo_set_source_rgb(cr, 0.85, 0.85, 0.85);
            } else {
                cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
            }

            // Set description text with middle ellipsization
            pango_layout_set_text(layout, dropdown_items.at(i).description.c_str(), -1);
            pango_layout_set_width(layout, available_width * PANGO_SCALE);
            pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_MIDDLE);

            // Draw description with some spacing after the title
            cairo_move_to(cr, left_margin + (title_width / PANGO_SCALE) + spacing, y_pos + 8);
            pango_cairo_show_layout(cr, layout);

            // Reset layout settings for next iteration
            pango_layout_set_width(layout, -1);
            pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
        }

        // Reset font for next iteration
        pango_layout_set_font_description(layout, font_desc);
    }

    // Flush to display
    cairo_surface_flush(surface);
}

} // namespace ui