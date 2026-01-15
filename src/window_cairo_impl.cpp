#include "config.h"
#include "fuzzy.h"
#include "glib-object.h"
#include "glib.h"
#include "logger.h"
#include "ui.h"
#include "window.h"

#include <cairo.h>
#include <cstddef>
#include <pango/pango-font.h>
#include <pango/pango-layout.h>
#include <pango/pango-types.h>
#include <pango/pangocairo.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace
{

// Corner flags for rounded rectangles
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

void set_color(cairo_t *cr, const Color &color)
{
    cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
}

double calculate_text_y_centered(double container_y, double container_height,
                                 int text_height_pango)
{
    return container_y +
           (container_height - (text_height_pango / PANGO_SCALE)) / 2.0;
}

void draw_rounded_rect(cairo_t *cr, double x, double y, double width,
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

} // anonymous namespace

void PlatformWindow::draw(const Config &config, const ui::State &state)
{
    const auto tik = std::chrono::steady_clock::now();

    // Get the cairo context for this platform
    cairo_t *cr = get_cairo_context();
    if (cr == nullptr) {
        throw std::runtime_error("Failed to get cairo context for drawing");
    }

    const double content_width = width - 2.0 * ui::BORDER_WIDTH;
    const size_t max_visible_items =
        ui::calculate_max_visible_items(height, config.font_size);

    // Create Pango layout for text rendering
    PangoLayout *layout = pango_cairo_create_layout(cr);
    const defer cleanup_layout([layout]() noexcept { g_object_unref(layout); });

    // Set font for launcher
    static PangoFontDescription *font_desc = nullptr;
    if (!font_desc) {
        font_desc = pango_font_description_from_string(
            (config.font_name + " " + std::to_string(config.font_size))
                .c_str());
    }
    // const defer cleanup_font(
    //    []() noexcept { pango_font_description_free(font_desc); });

    pango_layout_set_font_description(layout, font_desc);

    // Clear everything with transparent background
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    // Draw background
    set_color(cr, config.background_color);
    draw_rounded_rect(cr, 0, 0, width, height, ui::CORNER_RADIUS, Corner::All);
    cairo_fill(cr);
    // Border
    set_color(cr, config.input_background_color);
    cairo_set_line_width(cr, 1.0);
    draw_rounded_rect(cr, 0.0, 0.0, width - 1.0, height - 1.0, ui::CORNER_RADIUS, Corner::All);
    cairo_stroke(cr);

    // Draw Input Area
    const int input_height = ui::calculate_abs_input_height(config.font_size);
    draw_rounded_rect(cr, ui::BORDER_WIDTH, ui::BORDER_WIDTH, content_width,
                      input_height, ui::CORNER_RADIUS, Corner::All);

    // Use error styling if there's an error, otherwise normal styling
    if (state.has_error()) {
        cairo_set_source_rgba(cr, 1.0, 0.8, 0.8, 1.0); // Light red background
    } else {
        set_color(cr, config.input_background_color);
    }
    cairo_fill_preserve(cr);

    if (state.has_error()) {
        cairo_set_source_rgba(cr, 0.8, 0.0, 0.0, 1.0); // Dark red border
    } else {
        set_color(cr, config.selection_color);
    }
    cairo_stroke(cr);

    // Draw search prompt and buffer
    std::string display_text;
    if (state.has_error()) {
        // Show error message when there's an error
        display_text = *state.error_message;
    } else if (std::holds_alternative<ui::ContextMenu>(state.mode)) {
        // Show selected item title when in context menu
        display_text =
            std::get<ui::ContextMenu>(state.mode).title + " › Actions";
    } else {
        display_text = state.input_buffer;
        if (state.input_buffer.empty()) {
            const size_t total_files =
                state.cached_file_search_update.has_value()
                    ? state.cached_file_search_update->total_files
                    : 0;
            const std::string file_count_str =
                ui::format_file_count(total_files);
            display_text = "Search " + file_count_str +
                           " files... (prefix > for utility actions, ! "
                           "for applications)";
        }
    }

    pango_layout_set_text(layout, display_text.c_str(), -1);
    pango_layout_set_attributes(layout, nullptr);

    // Get text dimensions for vertical centering
    int text_width = 0;
    int text_height = 0;
    pango_layout_get_size(layout, &text_width, &text_height);
    const double input_area_y = ui::BORDER_WIDTH;
    const double text_y =
        calculate_text_y_centered(input_area_y, input_height, text_height);

    // Use dark red text for errors, normal text color otherwise
    if (state.has_error()) {
        cairo_set_source_rgba(cr, 0.7, 0.0, 0.0, 1.0); // Dark red text
    } else {
        set_color(cr, config.text_color);
    }
    cairo_move_to(cr, ui::BORDER_WIDTH + ui::INPUT_TEXT_MARGIN, text_y);
    pango_cairo_show_layout(cr, layout);

    // Draw progress indicator in file search mode, right-aligned
    if (std::holds_alternative<ui::FileSearch>(state.mode) &&
        state.cached_file_search_update.has_value() &&
        state.cached_file_search_update->total_files > 0) {

        const auto &update = *state.cached_file_search_update;
        std::string indicator_text;
        // Show scan status indicator when no query or no matches
        if (state.input_buffer.empty()) {
            indicator_text = update.scan_complete ? "✓" : "⟳";
        } else if (update.total_available_results == 0) {
            indicator_text = "0";
        } else {
            indicator_text = ui::create_pagination_text(
                state.visible_range_offset, max_visible_items,
                state.items.size(), update.total_available_results);
        }

        pango_layout_set_text(layout, indicator_text.c_str(), -1);

        // Get text dimensions for right alignment
        int count_width = 0;
        int count_height = 0;
        pango_layout_get_size(layout, &count_width, &count_height);

        // Choose color based on scan status: yellow if scanning, green if
        // complete
        if (update.scan_complete) {
            cairo_set_source_rgb(cr, 0.0, 0.8, 0.0); // Green
        } else {
            cairo_set_source_rgb(cr, 0.9, 0.9, 0.0); // Yellow
        }

        // Position at right edge of input area, with margin
        const double count_x = ui::BORDER_WIDTH + content_width -
                               (count_width / PANGO_SCALE) -
                               ui::INPUT_TEXT_MARGIN;
        const double count_y =
            calculate_text_y_centered(input_area_y, input_height, count_height);
        cairo_move_to(cr, count_x, count_y);
        pango_cairo_show_layout(cr, layout);

        // Restore layout for cursor drawing below
        pango_layout_set_text(layout, display_text.c_str(), -1);
        set_color(cr, config.text_color);
    }

    // Draw cursor at cursor position when not in context menu and no error
    if (!std::holds_alternative<ui::ContextMenu>(state.mode) &&
        !state.has_error()) {
        // Get width of text up to cursor position
        const std::string text_before_cursor =
            state.input_buffer.substr(0, state.cursor_position);
        pango_layout_set_text(layout, text_before_cursor.c_str(), -1);
        int cursor_x_offset = 0;
        int cursor_height = 0;
        pango_layout_get_size(layout, &cursor_x_offset, &cursor_height);

        // Draw cursor line
        set_color(cr, config.text_color);
        const double cursor_x = ui::BORDER_WIDTH + ui::INPUT_TEXT_MARGIN +
                                (cursor_x_offset / PANGO_SCALE);
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
        std::string hotkey_hint;
    };

    std::vector<DropdownItem> dropdown_items;

    const auto query_opt = ui::get_query(state.mode);
    const auto query_lower = to_lower(query_opt.value_or(""));
    dropdown_items.reserve(state.items.size());
    for (size_t idx = 0; idx < state.items.size(); ++idx) {
        const auto &item = state.items[idx];

        // Determine hotkey hint: use item's hotkey if set, otherwise show
        // Ctrl+1-9 for visible items at positions 0-8
        std::string hotkey_hint;
        if (item.hotkey.has_value()) {
            hotkey_hint = to_string(*item.hotkey);
        } else if (idx >= state.visible_range_offset &&
                   idx < state.visible_range_offset + 9) {
            // Show Ctrl+1-9 hints for first 9 visible items
            const size_t visible_pos = idx - state.visible_range_offset;
            hotkey_hint = "Ctrl+" + std::to_string(visible_pos + 1);
        }

        dropdown_items.push_back(DropdownItem{
            .title = item.title,
            .description = item.description,
            .title_match_positions =
                query_opt ? fuzzy::fuzzy_match_optimal(item.title, query_lower)
                          : std::vector<size_t>{},
            .description_match_positions =
                query_opt ? fuzzy::fuzzy_match_optimal(item.description, query_lower)
                          : std::vector<size_t>{},
            .hotkey_hint = hotkey_hint});
    }
    const auto selection_index = state.selected_item_index;

    // Calculate where dropdown items start (after input area + spacing)
    const double dropdown_start_y =
        ui::BORDER_WIDTH + input_height + ui::ITEMS_SPACING;

    // Draw dropdown items
    const int item_height = ui::calculate_abs_item_height(config.font_size);
    const size_t range_end = std::min(
        state.visible_range_offset + max_visible_items, dropdown_items.size());

    LOG_DEBUG("Drawing %ld dropdown items",
              range_end - state.visible_range_offset);

    for (size_t i = state.visible_range_offset; i < range_end; ++i) {
        const double y_pos =
            dropdown_start_y +
            (static_cast<double>(i - state.visible_range_offset) *
             static_cast<double>(item_height));

        // Draw selection highlight
        const bool item_is_selected = (i == selection_index);
        if (item_is_selected) {
            set_color(cr, config.selection_color);
            draw_rounded_rect(cr, ui::BORDER_WIDTH, y_pos, content_width,
                              item_height, ui::CORNER_RADIUS, Corner::All);
            cairo_fill(cr);
        }

        // Set text color (selected vs normal)
        if (item_is_selected) {
            set_color(cr, config.selection_text_color);
        } else {
            set_color(cr, config.text_color);
        }

        // Draw the title text
        pango_layout_set_text(layout, dropdown_items.at(i).title.c_str(), -1);
        // Draw icon and filename (main text) with highlighting - center
        // vertically within item_height
        const std::string highlighted_title = ui::create_highlighted_markup(
            dropdown_items.at(i).title,
            dropdown_items.at(i).title_match_positions);
        pango_layout_set_markup(layout, highlighted_title.c_str(), -1);
        int text_width_unused, item_text_height = 0;
        pango_layout_get_size(layout, &text_width_unused, &item_text_height);
        const double text_y_centered =
            calculate_text_y_centered(y_pos, item_height, item_text_height);
        cairo_move_to(cr, ui::BORDER_WIDTH + ui::TEXT_MARGIN, text_y_centered);
        pango_layout_set_width(
            layout, static_cast<int>((content_width - 2 * ui::TEXT_MARGIN) *
                                     PANGO_SCALE));
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_MIDDLE);
        pango_cairo_show_layout(cr, layout);
        pango_layout_set_width(layout, -1);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);

        // Draw description to the right of the title in subtle grey
        if (!dropdown_items.at(i).description.empty()) {
            // Get the width of the title text
            int title_width = 0;
            int title_height = 0;
            pango_layout_get_size(layout, &title_width, &title_height);

            // Calculate available width for description
            const int available_width = static_cast<int>(
                content_width - 2 * ui::TEXT_MARGIN -
                (title_width / PANGO_SCALE) - ui::DESCRIPTION_SPACING);

            // Set description color
            if (item_is_selected) {
                set_color(cr, config.selection_description_color);
            } else {
                set_color(cr, config.description_color);
            }

            // Set description text with highlighting and middle
            // ellipsization
            const std::string highlighted_description =
                ui::create_highlighted_markup(
                    dropdown_items.at(i).description,
                    dropdown_items.at(i).description_match_positions);
            pango_layout_set_markup(layout, highlighted_description.c_str(),
                                    -1);
            pango_layout_set_width(layout, available_width * PANGO_SCALE);
            pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_MIDDLE);

            // Draw description with some spacing after the title
            cairo_move_to(cr,
                          ui::BORDER_WIDTH + ui::TEXT_MARGIN +
                              (title_width / PANGO_SCALE) +
                              ui::DESCRIPTION_SPACING,
                          text_y_centered);
            pango_cairo_show_layout(cr, layout);

            // Reset layout settings for next iteration
            pango_layout_set_width(layout, -1);
            pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
        }

        // Draw hotkey hint on the right side of the item
        if (!dropdown_items.at(i).hotkey_hint.empty()) {
            // Set subtle color for hotkey hint
            if (item_is_selected) {
                set_color(cr, config.selection_description_color);
            } else {
                set_color(cr, config.description_color);
            }

            pango_layout_set_text(layout, dropdown_items.at(i).hotkey_hint.c_str(), -1);

            // Get hint text dimensions
            int hint_width = 0;
            int hint_height = 0;
            pango_layout_get_size(layout, &hint_width, &hint_height);

            // Position at far right of item area
            const double hint_x = ui::BORDER_WIDTH + content_width -
                                  (hint_width / PANGO_SCALE) - ui::TEXT_MARGIN;
            const double hint_y =
                calculate_text_y_centered(y_pos, item_height, hint_height);

            cairo_move_to(cr, hint_x, hint_y);
            pango_cairo_show_layout(cr, layout);
        }

        // Reset font for next iteration
        pango_layout_set_font_description(layout, font_desc);
    }

    const auto tok = std::chrono::steady_clock::now();
    LOG_DEBUG("Cairo draw call took %ld ms",
              std::chrono::duration_cast<std::chrono::milliseconds>(tok - tik)
                  .count());
}
