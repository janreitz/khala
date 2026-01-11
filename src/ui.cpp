#include "actions.h"
#include "ui.h"
#include "fuzzy.h"
#include "window.h"
#include "logger.h"

#include <cairo.h>

#include "pango/pango-font.h"
#include "pango/pango-layout.h"
#include "pango/pango-types.h"
#include <pango/pangocairo.h>

#include <algorithm>
#include <cstdint>
#include <optional>
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

std::string format_file_count(size_t count)
{
    if (count >= 1'000'000) {
        double millions = static_cast<double>(count) / 1'000'000.0;
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.1fM", millions);
        return buffer;
    } else if (count >= 1'000) {
        double thousands = static_cast<double>(count) / 1'000.0;
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.1fK", thousands);
        return buffer;
    } else {
        return std::to_string(count);
    }
}

std::string create_pagination_text(size_t visible_offset,
                                   size_t max_visible_items,
                                   size_t total_results,
                                   size_t total_available_results)
{
    if (total_available_results == 0 || total_results <= max_visible_items) {
        return ""; // No pagination needed
    }

    const size_t visible_start = visible_offset + 1; // 1-indexed for display
    const size_t visible_end =
        std::min(visible_offset + max_visible_items, total_results);

    char buffer[128];
    snprintf(buffer, sizeof(buffer), "%zu-%zu/%s", visible_start, visible_end,
             format_file_count(total_available_results).c_str());
    return buffer;
}

std::string
create_highlighted_markup(const std::string &text,
                          const std::vector<size_t> &match_positions)
{
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

int calculate_abs_input_height(int font_size)
{
    return static_cast<int>(font_size + 2 * INPUT_VERTICAL_PADDING);
}

int calculate_abs_item_height(int font_size)
{
    return static_cast<int>(font_size + 2 * ITEM_VERTICAL_PADDING);
}

size_t calculate_max_visible_items(unsigned int window_height, int font_size)
{
    const int input_height = calculate_abs_input_height(font_size);
    const int item_height = calculate_abs_item_height(font_size);

    // Calculate available space for items
    const int available_for_items =
        static_cast<int>(window_height) - static_cast<int>(2 * BORDER_WIDTH) - input_height -
        static_cast<int>(ITEMS_SPACING);

    // Calculate max visible items that can fit in available space
    return static_cast<size_t>(std::max(1, available_for_items / item_height));
}

unsigned int calculate_window_height(int font_size, size_t item_count,
                            size_t max_visible_items)
{
    const size_t visible_items = std::min(item_count, max_visible_items);

    // Account for: top border + input area + spacing + items + bottom border
    return static_cast<unsigned int>(
        2 * BORDER_WIDTH + calculate_abs_input_height(font_size) +
        ITEMS_SPACING + (static_cast<double>(visible_items) * calculate_abs_item_height(font_size)));
}

Item State::get_selected_item() const { return items.at(selected_item_index); }

void State::set_error(const std::optional<std::string> &message)
{
    if (message) {
        error_message = *message + " (Esc to clear)";
    } else {
        error_message = std::nullopt;
    }
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

void draw(cairo_t *cr, unsigned int window_width, unsigned int window_height,
          const Config &config, const State &state)
{
    const double content_width = window_width - 2.0 * BORDER_WIDTH;
    const size_t max_visible_items = calculate_max_visible_items(window_height, config.font_size);

    // Create Pango layout for text rendering
    PangoLayout *layout = pango_cairo_create_layout(cr);
    const defer cleanup_layout([layout]() noexcept { g_object_unref(layout); });

    // Clear everything with transparent background
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    // Draw background
    set_color(cr, config.background_color);
    draw_rounded_rect(cr, 0, 0, window_width, window_height, CORNER_RADIUS,
                      Corner::All);
    cairo_fill(cr);

    // Draw Input Area
    const int input_height = calculate_abs_input_height(config.font_size);
    draw_rounded_rect(cr, BORDER_WIDTH, BORDER_WIDTH, content_width,
                      input_height, CORNER_RADIUS, Corner::All);

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

    // Set font for launcher
    PangoFontDescription *font_desc = pango_font_description_from_string(
        (config.font_name + " " + std::to_string(config.font_size)).c_str());
    const defer cleanup_font(
        [font_desc]() noexcept { pango_font_description_free(font_desc); });
    pango_layout_set_font_description(layout, font_desc);

    // Draw search prompt and buffer
    std::string display_text;
    if (state.has_error()) {
        // Show error message when there's an error
        display_text = *state.error_message;
    } else if (std::holds_alternative<ContextMenu>(state.mode)) {
        // Show selected item title when in context menu
        display_text =
            std::get<ContextMenu>(state.mode).title +
            " › Actions";
    } else {
        display_text = state.input_buffer;
        if (state.input_buffer.empty()) {
            const size_t total_files = state.cached_file_search_update.has_value()
                ? state.cached_file_search_update->total_files
                : 0;
            const std::string file_count_str = format_file_count(total_files);
            display_text = "Search " + file_count_str +
                           " files... (prefix > for utility actions, ! "
                           "for applications)";
        }
    }

    pango_layout_set_text(layout, display_text.c_str(), -1);
    pango_layout_set_attributes(layout, nullptr);

    // Get text dimensions for vertical centering
    int text_width;
    int text_height;
    pango_layout_get_size(layout, &text_width, &text_height);
    const double input_area_y = BORDER_WIDTH;
    const double text_y =
        calculate_text_y_centered(input_area_y, input_height, text_height);

    // Use dark red text for errors, normal text color otherwise
    if (state.has_error()) {
        cairo_set_source_rgba(cr, 0.7, 0.0, 0.0, 1.0); // Dark red text
    } else {
        set_color(cr, config.text_color);
    }
    cairo_move_to(cr, BORDER_WIDTH + INPUT_TEXT_MARGIN, text_y);
    pango_cairo_show_layout(cr, layout);

    // Draw progress indicator in file search mode, right-aligned
    if (std::holds_alternative<ui::FileSearch>(state.mode) &&
        state.cached_file_search_update.has_value() &&
        state.cached_file_search_update->total_files > 0) {

        const auto& update = *state.cached_file_search_update;
        std::string indicator_text;
        // Show scan status indicator when no query or no matches
        if (state.input_buffer.empty()) {
            indicator_text = update.scan_complete ? "✓" : "⟳";
        } else if (update.total_available_results == 0) {
            indicator_text = "0";
        } else {
            indicator_text = create_pagination_text(
                state.visible_range_offset, max_visible_items,
                state.items.size(), update.total_available_results);
        }

        pango_layout_set_text(layout, indicator_text.c_str(), -1);

        // Get text dimensions for right alignment
        int count_width, count_height;
        pango_layout_get_size(layout, &count_width, &count_height);

        // Choose color based on scan status: yellow if scanning, green if
        // complete
        if (update.scan_complete) {
            cairo_set_source_rgb(cr, 0.0, 0.8, 0.0); // Green
        } else {
            cairo_set_source_rgb(cr, 0.9, 0.9, 0.0); // Yellow
        }

        // Position at right edge of input area, with margin
        const double count_x = BORDER_WIDTH + content_width -
                               (count_width / PANGO_SCALE) - INPUT_TEXT_MARGIN;
        const double count_y =
            calculate_text_y_centered(input_area_y, input_height, count_height);
        cairo_move_to(cr, count_x, count_y);
        pango_cairo_show_layout(cr, layout);

        // Restore layout for cursor drawing below
        pango_layout_set_text(layout, display_text.c_str(), -1);
        set_color(cr, config.text_color);
    }

    // Draw cursor at cursor position when not in context menu and no error
    if (!std::holds_alternative<ContextMenu>(state.mode) &&
        !state.has_error()) {
        // Get width of text up to cursor position
        const std::string text_before_cursor =
            state.input_buffer.substr(0, state.cursor_position);
        pango_layout_set_text(layout, text_before_cursor.c_str(), -1);
        int cursor_x_offset;
        int cursor_height;
        pango_layout_get_size(layout, &cursor_x_offset, &cursor_height);

        // Draw cursor line
        set_color(cr, config.text_color);
        const double cursor_x =
            BORDER_WIDTH + INPUT_TEXT_MARGIN + (cursor_x_offset / PANGO_SCALE);
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
            .title_match_positions =
                query_opt ? fuzzy::fuzzy_match_optimal(item.title, query)
                          : std::vector<size_t>{},
            .description_match_positions =
                query_opt ? fuzzy::fuzzy_match_optimal(item.description, query)
                          : std::vector<size_t>{}});
    }
    const auto selection_index = state.selected_item_index;

    // Calculate where dropdown items start (after input area + spacing)
    const double dropdown_start_y = BORDER_WIDTH + input_height + ITEMS_SPACING;

    // Draw dropdown items
    const int item_height = calculate_abs_item_height(config.font_size);
    const size_t range_end = std::min(
        state.visible_range_offset + max_visible_items, dropdown_items.size());
    for (size_t i = state.visible_range_offset; i < range_end; ++i) {
        const double y_pos =
            dropdown_start_y + (static_cast<double>(i - state.visible_range_offset) * static_cast<double>(item_height));

        // Draw selection highlight
        const bool item_is_selected = (i == selection_index);
        if (item_is_selected) {
            set_color(cr, config.selection_color);
            draw_rounded_rect(cr, BORDER_WIDTH, y_pos, content_width,
                              item_height, CORNER_RADIUS, Corner::All);
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
        const std::string highlighted_title = create_highlighted_markup(
            dropdown_items.at(i).title,
            dropdown_items.at(i).title_match_positions);
        pango_layout_set_markup(layout, highlighted_title.c_str(), -1);
        int text_width_unused, item_text_height;
        pango_layout_get_size(layout, &text_width_unused, &item_text_height);
        const double text_y_centered =
            calculate_text_y_centered(y_pos, item_height, item_text_height);
        cairo_move_to(cr, BORDER_WIDTH + TEXT_MARGIN, text_y_centered);
        pango_layout_set_width(layout,
                               static_cast<int>((content_width - 2 * TEXT_MARGIN) * PANGO_SCALE));
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_MIDDLE);
        pango_cairo_show_layout(cr, layout);
        pango_layout_set_width(layout, -1);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);

        // Draw description to the right of the title in subtle grey
        if (!dropdown_items.at(i).description.empty()) {
            // Get the width of the title text
            int title_width;
            int title_height;
            pango_layout_get_size(layout, &title_width, &title_height);

            // Calculate available width for description
            const int available_width = static_cast<int>(content_width - 2 * TEXT_MARGIN -
                                        (title_width / PANGO_SCALE) -
                                        DESCRIPTION_SPACING);

            // Set description color
            if (item_is_selected) {
                set_color(cr, config.selection_description_color);
            } else {
                set_color(cr, config.description_color);
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
                          BORDER_WIDTH + TEXT_MARGIN +
                              (title_width / PANGO_SCALE) + DESCRIPTION_SPACING,
                          text_y_centered);
            pango_cairo_show_layout(cr, layout);

            // Reset layout settings for next iteration
            pango_layout_set_width(layout, -1);
            pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
        }

        // Reset font for next iteration
        pango_layout_set_font_description(layout, font_desc);
    }
}

std::vector<Event> handle_keyboard_input(State &state,
                                         const KeyboardEvent &kbd_event,
                                         const Config &config)
{
    std::vector<Event> events;

    switch (kbd_event.key) {
    case KeyCode::Escape:
        events.push_back(ExitRequested{});
        break;

    case KeyCode::Up:
        if (!state.items.empty()) {
            if (state.selected_item_index > 0) {
                state.selected_item_index--;
            } else {
                state.selected_item_index = state.items.size() - 1;
            }
            events.push_back(SelectionChanged{});
        }
        break;

    case KeyCode::Down:
        if (!state.items.empty()) {
            if (state.selected_item_index < state.items.size() - 1) {
                state.selected_item_index++;
            } else {
                state.selected_item_index = 0;
            }
            events.push_back(SelectionChanged{});
        }
        break;

    case KeyCode::Tab:
        if (!std::holds_alternative<ContextMenu>(state.mode) &&
            !state.items.empty()) {
            const auto &file_item = state.get_selected_item();
            if (!file_item.path.has_value()) {
                break;
            }
            state.mode = ContextMenu{.title = file_item.title, .selected_file = file_item.path.value()};
            state.selected_item_index = 0;
            state.items = make_file_actions(file_item.path.value(), config);
            events.push_back(ContextMenuToggled{});
        }
        break;

    case KeyCode::Left:
        if (std::holds_alternative<ContextMenu>(state.mode)) {
            state.mode = FileSearch{.query = state.input_buffer};
            events.push_back(ContextMenuToggled{});
        } else {
            if (state.cursor_position > 0) {
                state.cursor_position--;
                events.push_back(CursorPositionChanged{});
            }
        }
        break;

    case KeyCode::Right:
        if (!std::holds_alternative<ContextMenu>(state.mode) &&
            state.cursor_position < state.input_buffer.size()) {
            state.cursor_position++;
            events.push_back(CursorPositionChanged{});
        }
        break;

    case KeyCode::Home:
        if (!std::holds_alternative<ContextMenu>(state.mode)) {
            state.cursor_position = 0;
            events.push_back(CursorPositionChanged{});
        }
        break;

    case KeyCode::End:
        if (!std::holds_alternative<ContextMenu>(state.mode)) {
            state.cursor_position = state.input_buffer.size();
            events.push_back(CursorPositionChanged{});
        }
        break;

    case KeyCode::Return:
        events.push_back(ActionRequested{});
        break;

    case KeyCode::BackSpace:
        state.clear_error(); // Clear error when user starts typing
        if (!state.input_buffer.empty() && state.cursor_position > 0) {
            state.input_buffer.erase(state.cursor_position - 1, 1);
            state.cursor_position--;
            events.push_back(InputChanged{});
        }
        break;

    case KeyCode::Character:
        state.clear_error(); // Clear error when user starts typing
        if (kbd_event.character >= 32 && kbd_event.character < 127) {
            state.input_buffer.insert(state.cursor_position, 1,
                                      *kbd_event.character);
            state.cursor_position++;
            events.push_back(InputChanged{});
        }
        break;
    }

    return events;
}

std::vector<Event> handle_user_input(State &state, const UserInputEvent &input,
                                     const Config &config)
{
    std::vector<Event> events;
    state.clear_error();

    std::visit(overloaded{[&](const KeyboardEvent &ev) {
                   events = handle_keyboard_input(state, ev, config);
               }},
               input);

    return events;
}

bool adjust_visible_range(State &state, size_t max_visible_items)
{
    const auto old_visible_range_offset = state.visible_range_offset;
    if (state.items.empty()) {
        state.visible_range_offset = 0;
    }

    // Adjust visible range to keep selected item visible
    if (state.selected_item_index < state.visible_range_offset) {
        // Selected item is above visible range, scroll up
        state.visible_range_offset = state.selected_item_index;
    } else if (state.selected_item_index >=
               state.visible_range_offset + max_visible_items) {
        // Selected item is below visible range, scroll down
        state.visible_range_offset =
            state.selected_item_index - max_visible_items + 1;
    }

    return state.visible_range_offset != old_visible_range_offset;
}

size_t required_item_count(const State &state, size_t max_visible_items)
{
    return state.visible_range_offset + (max_visible_items * 2);
}

std::vector<Item> convert_file_results_to_items(
    const std::vector<FileResult> &file_results)
{
    std::vector<Item> items;
    items.reserve(file_results.size());

    for (const auto &result : file_results) {
        try {
            const auto file_path = fs::canonical(result.path);

            if (fs::is_directory(file_path)) {
                items.push_back(Item{
                    .title = "📁 " + path_to_string(file_path),
                    .description = serialize_file_info(file_path),
                    .path = file_path,
                    .command = OpenDirectory{.path = file_path},
                });
            } else {
                items.push_back(Item{
                    .title = "📄 " + path_to_string(file_path),
                    .description = serialize_file_info(file_path),
                    .path = file_path,
                    .command = OpenFileCommand{.path = file_path},
                });
            }
        } catch (const std::exception &e) {
            LOG_WARNING("Could not make canonical path for %s: %s",
                   result.path.c_str(), e.what());
        }
    }

    return items;
}

} // namespace ui