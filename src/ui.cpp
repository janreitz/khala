#include "ui.h"
#include "fuzzy.h"
#include "utility.h"

#include <cairo-xlib.h>
#include <cairo.h>

#include "pango/pango-font.h"
#include "pango/pango-layout.h"
#include "pango/pango-types.h"
#include <pango/pangocairo.h>

#include <algorithm>
#include <array>
#include <string>

namespace ui
{

std::optional<std::string> get_query(const AppMode &mode)
{
    return std::visit(
        [](auto &m) -> std::optional<std::string> {
            if constexpr (requires { m.query; }) {
                return m.query;
            } else {
                return std::nullopt;
            }
        },
        mode);
};

std::string
create_highlighted_markup(const std::string &text,
                          const std::vector<size_t> &match_positions)
{
    if (match_positions.empty()) {
        // No highlighting needed, escape the text for markup
        std::string escaped;
        for (char c : text) {
            if (c == '&')
                escaped += "&amp;";
            else if (c == '<')
                escaped += "&lt;";
            else if (c == '>')
                escaped += "&gt;";
            else
                escaped += c;
        }
        return escaped;
    }

    std::string result;
    size_t match_idx = 0;

    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];

        // Check if this position should be highlighted
        bool should_highlight = (match_idx < match_positions.size() &&
                                 match_positions[match_idx] == i);

        if (should_highlight) {
            result += "<b>";
        }

        // Escape special markup characters
        if (c == '&')
            result += "&amp;";
        else if (c == '<')
            result += "&lt;";
        else if (c == '>')
            result += "&gt;";
        else
            result += c;

        if (should_highlight) {
            result += "</b>";
            match_idx++;
        }
    }

    return result;
}

int calculate_actual_input_height(const Config &config, int screen_height)
{
    return static_cast<int>(screen_height * config.input_height_ratio);
}

int calculate_actual_item_height(const Config &config, int screen_height)
{
    return static_cast<int>(screen_height * config.item_height_ratio);
}

int calculate_window_height(const Config &config, const State &state,
                            int screen_height)
{
    const size_t item_count = state.items.size();
    const size_t visible_items = std::min(item_count, config.max_visible_items);
    const int input_height =
        calculate_actual_input_height(config, screen_height);
    const int item_height = calculate_actual_item_height(config, screen_height);
    return input_height + (visible_items * item_height);
}

Item State::get_selected_item() const { return items.at(selected_item_index); }

void State::set_error(const std::string &message)
{
    error_message = message + " (Esc to clear)";
}

void State::clear_error() { error_message = std::nullopt; }

bool State::has_error() const { return error_message.has_value(); }

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

    if (corners & Corner::TopRight) {
        cairo_arc(cr, x + width - radius, y + radius, radius, -90 * degrees,
                  0 * degrees);
    } else {
        cairo_move_to(cr, x + width, y);
    }

    if (corners & Corner::BottomRight) {
        cairo_arc(cr, x + width - radius, y + height - radius, radius,
                  0 * degrees, 90 * degrees);
    } else {
        cairo_line_to(cr, x + width, y + height);
    }

    if (corners & Corner::BottomLeft) {
        cairo_arc(cr, x + radius, y + height - radius, radius, 90 * degrees,
                  180 * degrees);
    } else {
        cairo_line_to(cr, x, y + height);
    }

    if (corners & Corner::TopLeft) {
        cairo_arc(cr, x + radius, y + radius, radius, 180 * degrees,
                  270 * degrees);
    } else {
        cairo_line_to(cr, x, y);
    }

    cairo_close_path(cr);
}

void draw(XWindow &window, const Config &config, const State &state)
{
    // Calculate actual heights based on screen size
    const int input_height =
        calculate_actual_input_height(config, window.screen_height);
    const int item_height =
        calculate_actual_item_height(config, window.screen_height);

    // Calculate window height based on item count
    const int new_height =
        calculate_window_height(config, state, window.screen_height);
    if (new_height != window.height) {
        XResizeWindow(window.display, window.window, window.width, new_height);
        window.height = new_height;
    }

    // Get the window's visual (which should be ARGB for transparency)
    XWindowAttributes window_attrs;
    XGetWindowAttributes(window.display, window.window, &window_attrs);
    Visual *visual = window_attrs.visual;

    // Create Cairo surface for X11 window
    cairo_surface_t *surface = cairo_xlib_surface_create(
        window.display, window.window, visual, window.width, window.height);
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

    // Fill entire window with input background color
    draw_rounded_rect(cr, 0, 0, window.width, window.height, corner_radius,
                      Corner::All);
    const auto input_bg = config.input_background_color;
    cairo_set_source_rgb(cr, input_bg.r, input_bg.g, input_bg.b);
    cairo_fill(cr);

    // Draw white background for dropdown area if there are items
    if (window.height > input_height) {
        const auto bg_color = config.background_color;
        cairo_set_source_rgb(cr, bg_color.r, bg_color.g, bg_color.b);
        draw_rounded_rect(cr, 0, input_height, window.width,
                          window.height - input_height, corner_radius,
                          Corner::BottomLeft | Corner::BottomRight);
        cairo_fill(cr);
    }

    // Draw border around entire window
    draw_rounded_rect(cr, 0, 0, window.width, window.height, corner_radius,
                      Corner::All);
    const auto border = config.border_color;
    cairo_set_source_rgb(cr, border.r, border.g, border.b);
    cairo_set_line_width(cr, border_width);
    cairo_stroke(cr);

    // Set font for launcher
    PangoFontDescription *font_desc = pango_font_description_from_string(
        (config.font_name + " " + std::to_string(config.font_size)).c_str());
    const defer cleanup_font(
        [font_desc]() noexcept { pango_font_description_free(font_desc); });
    pango_layout_set_font_description(layout, font_desc);

    // Draw search prompt and buffer
    std::string display_text;
    if (std::holds_alternative<ContextMenu>(state.mode)) {
        // Show selected item title when in context menu
        display_text =
            fs::canonical(std::get<ContextMenu>(state.mode).selected_file)
                .generic_string() +
            " › Actions";
    } else {
        display_text = state.input_buffer;
        if (state.input_buffer.empty()) {
            display_text = "Search files... (prefix > for utility actions, ! "
                           "for applications)";
        }
    }

    // TODO add right aligned indicator of indexed file count

    pango_layout_set_text(layout, display_text.c_str(), -1);
    pango_layout_set_attributes(layout, nullptr);

    // Get text dimensions for vertical centering
    int text_width;
    int text_height;
    pango_layout_get_size(layout, &text_width, &text_height);
    const double text_y = (input_height - (text_height / PANGO_SCALE)) / 2.0;

    const auto text = config.text_color;
    cairo_set_source_rgb(cr, text.r, text.g, text.b);
    cairo_move_to(cr, 10, text_y);
    pango_cairo_show_layout(cr, layout);

    // Draw cursor at cursor position when not in context menu
    if (!std::holds_alternative<ContextMenu>(state.mode)) {
        // Get width of text up to cursor position
        const std::string text_before_cursor =
            state.input_buffer.substr(0, state.cursor_position);
        pango_layout_set_text(layout, text_before_cursor.c_str(), -1);
        int cursor_x_offset;
        int cursor_height;
        pango_layout_get_size(layout, &cursor_x_offset, &cursor_height);

        // Draw cursor line
        cairo_set_source_rgb(cr, text.r, text.g, text.b);
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
        std::vector<size_t> title_match_positions;
        std::vector<size_t> description_match_positions;
        // Add style info
    };

    std::vector<DropdownItem> dropdown_items;

    const auto query_opt = get_query(state.mode);
    const std::string query = query_opt.value_or("");
    dropdown_items.reserve(state.items.size());
    for (const auto &item : state.items) {
        dropdown_items.push_back(DropdownItem{
            .title = item.title,
            .description = item.description,
            .title_match_positions = query_opt
                                         ? fuzzy::fuzzy_match(item.title, query)
                                         : std::vector<size_t>{},
            .description_match_positions =
                query_opt ? fuzzy::fuzzy_match(item.description, query)
                          : std::vector<size_t>{}});
    }
    const auto selection_index = state.selected_item_index;

    // Draw dropdown items
    for (size_t i = 0; i < dropdown_items.size(); ++i) {
        const int y_pos = input_height + (i * item_height);

        // Draw selection highlight
        if (i == selection_index) {
            const auto sel = config.selection_color;
            cairo_set_source_rgb(cr, sel.r, sel.g, sel.b);

            // Use rounded bottom corners if this is the last item
            const bool is_last_item = (i == dropdown_items.size() - 1);
            if (is_last_item) {
                draw_rounded_rect(cr, 0, y_pos, window.width, item_height,
                                  corner_radius,
                                  Corner::BottomLeft | Corner::BottomRight);
            } else {
                draw_rounded_rect(cr, 0, y_pos, window.width, item_height, 0,
                                  Corner::NoCorners);
            }
            cairo_fill(cr);
        }

        // Set text color (selected vs normal)
        if (i == selection_index) {
            const auto sel_text = config.selection_text_color;
            cairo_set_source_rgb(cr, sel_text.r, sel_text.g, sel_text.b);
        } else {
            cairo_set_source_rgb(cr, text.r, text.g, text.b);
        }

        // Draw the title text
        pango_layout_set_text(layout, dropdown_items.at(i).title.c_str(), -1);
        // Draw icon and filename (main text) with highlighting - center
        // vertically within item_height
        const std::string highlighted_title = create_highlighted_markup(
            dropdown_items.at(i).title,
            dropdown_items.at(i).title_match_positions);
        pango_layout_set_markup(layout, highlighted_title.c_str(), -1);
        int text_width_unused, text_height;
        pango_layout_get_size(layout, &text_width_unused, &text_height);
        const double text_y_centered =
            y_pos + (item_height - (text_height / PANGO_SCALE)) / 2.0;
        cairo_move_to(cr, 15, text_y_centered);
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
            const int available_width = window.width - left_margin -
                                        (title_width / PANGO_SCALE) - spacing -
                                        right_margin;

            // Set description color
            if (i == selection_index) {
                const auto sel_desc = config.selection_description_color;
                cairo_set_source_rgb(cr, sel_desc.r, sel_desc.g, sel_desc.b);
            } else {
                const auto desc = config.description_color;
                cairo_set_source_rgb(cr, desc.r, desc.g, desc.b);
            }

            // Set description text with highlighting and middle
            // ellipsization
            const std::string highlighted_description =
                create_highlighted_markup(
                    dropdown_items.at(i).description,
                    dropdown_items.at(i).description_match_positions);
            pango_layout_set_markup(layout, highlighted_description.c_str(),
                                    -1);
            pango_layout_set_width(layout, available_width * PANGO_SCALE);
            pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_MIDDLE);

            // Draw description with some spacing after the title
            cairo_move_to(cr,
                          left_margin + (title_width / PANGO_SCALE) + spacing,
                          text_y_centered);
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