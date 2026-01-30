#include "ui.h"
#include "actions.h"
#include "config.h"
#include "logger.h"
#include "ranker.h"
#include "types.h"
#include "utility.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <variant>
#include <vector>

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

std::string format_file_count(size_t count)
{
    if (count >= 1'000'000) {
        const double millions = static_cast<double>(count) / 1'000'000.0;
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.1fM", millions);
        return buffer;
    } else if (count >= 1'000) {
        const double thousands = static_cast<double>(count) / 1'000.0;
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
        const char c = text[i];

        // Check if this position should be highlighted
        const bool should_highlight = (match_idx < match_positions.size() &&
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
        static_cast<int>(window_height) - static_cast<int>(2 * BORDER_WIDTH) -
        input_height - static_cast<int>(ITEMS_SPACING);

    // Calculate max visible items that can fit in available space
    return static_cast<size_t>(std::max(1, available_for_items / item_height));
}

unsigned int calculate_window_height(int font_size, size_t item_count,
                                     size_t max_visible_items)
{
    const size_t visible_items = std::min(item_count, max_visible_items);
    // top border + input area + bottom border
    const auto input_area_height = static_cast<unsigned int>(
        2 * BORDER_WIDTH + calculate_abs_input_height(font_size));
    if (item_count == 0) {
        return input_area_height;
    }

    return input_area_height +
           static_cast<unsigned int>(ITEMS_SPACING +
                                     (static_cast<double>(visible_items) *
                                      calculate_abs_item_height(font_size)));
}

std::optional<size_t> window_pos_to_item_index(const WindowCoord &position,
                                               const State &state,
                                               int font_size)
{
    // Calculate layout dimensions
    const int input_height = calculate_abs_input_height(font_size);
    const int item_height = calculate_abs_item_height(font_size);

    // Calculate where the dropdown area starts
    const int dropdown_start_y = static_cast<int>(BORDER_WIDTH) + input_height +
                                 static_cast<int>(ITEMS_SPACING);

    // Check if position is in input area or above
    if (position.y < dropdown_start_y) {
        return std::nullopt;
    }

    // Calculate relative Y position in dropdown area
    const int relative_y = position.y - dropdown_start_y;

    // Calculate visible item index
    const size_t visible_item_index =
        static_cast<size_t>(relative_y / item_height);

    // Convert to absolute index
    const size_t absolute_index =
        state.visible_range_offset + visible_item_index;

    // Validate bounds
    if (absolute_index >= state.items.size()) {
        return std::nullopt;
    }

    // Check if we're within the visible range
    if (visible_item_index >= state.max_visible_items) {
        return std::nullopt;
    }

    return absolute_index;
}

std::optional<Item> State::get_selected_item() const
{
    if (selected_item_index >= items.size()) {
        // Covers items.empty() as well as out-of-range index
        return std::nullopt;
    }
    return items[selected_item_index];
}

void State::push_error(const std::string &error)
{
    if (!std::holds_alternative<ErrorMode>(mode)) {
        mode = ErrorMode{};
        items.clear();
    }
    selected_item_index = std::numeric_limits<size_t>::max();
    items.push_back(ui::Item{
        .title = "⚠ " + error,
        .description = "",
        .path = std::nullopt,
        .command = Noop{},
        .hotkey = std::nullopt,
    });
}

bool State::has_errors() const
{
    return std::holds_alternative<ErrorMode>(mode);
}

void State::clear_errors()
{
    mode = ui::FileSearch{
        .query = input_buffer,
    };
}

// Try to open context menu for the currently selected file item
// Returns true if context menu was opened, false otherwise
static bool try_open_context_menu(State &state, const Config &config)
{
    // Only open context menu in FileSearch mode
    if (!std::holds_alternative<FileSearch>(state.mode)) {
        return false;
    }

    // Need items to show context menu for
    if (state.items.empty()) {
        return false;
    }

    const auto file_item = state.get_selected_item();
    if (!file_item) {
        return false;
    }
    if (!file_item->path.has_value()) {
        return false;
    }

    // Open context menu
    state.mode = ContextMenu{.title = file_item->title,
                             .selected_file = file_item->path.value()};
    state.selected_item_index = 0;
    state.items = make_file_actions(file_item->path.value(), config);
    return true;
}

// Helper to check if a KeyboardEvent matches a specific key with Ctrl modifier
static bool is_ctrl_number(const KeyboardEvent &ev, KeyCode num_key)
{
    return ev.key == num_key && has_modifier(ev.modifiers, KeyModifier::Ctrl) &&
           !has_modifier(ev.modifiers, KeyModifier::Alt) &&
           !has_modifier(ev.modifiers, KeyModifier::Super);
}

// Map Ctrl+1-0 to item index (0-9), returns nullopt if not a Ctrl+number
static std::optional<size_t> get_ctrl_number_index(const KeyboardEvent &ev)
{
    if (is_ctrl_number(ev, KeyCode::Num1))
        return 0;
    if (is_ctrl_number(ev, KeyCode::Num2))
        return 1;
    if (is_ctrl_number(ev, KeyCode::Num3))
        return 2;
    if (is_ctrl_number(ev, KeyCode::Num4))
        return 3;
    if (is_ctrl_number(ev, KeyCode::Num5))
        return 4;
    if (is_ctrl_number(ev, KeyCode::Num6))
        return 5;
    if (is_ctrl_number(ev, KeyCode::Num7))
        return 6;
    if (is_ctrl_number(ev, KeyCode::Num8))
        return 7;
    if (is_ctrl_number(ev, KeyCode::Num9))
        return 8;
    if (is_ctrl_number(ev, KeyCode::Num0))
        return 9;
    return std::nullopt;
}

// Check if keyboard event matches an item's hotkey
static bool hotkey_matches(const KeyboardEvent &ev, const KeyboardEvent &hotkey)
{
    return ev.key == hotkey.key && ev.modifiers == hotkey.modifiers;
}

std::vector<Event> handle_keyboard_input(State &state,
                                         const KeyboardEvent &kbd_event,
                                         const Config &config)
{
    // If in ErrorMode, any input dismisses the errors and returns to FileSearch
    if (state.has_errors()) {
        state.clear_errors();
        return {InputChanged{}}; // For redraw
    }

    // Check for quit hotkey first
    if (kbd_event.key == config.quit_hotkey.key &&
        kbd_event.modifiers == config.quit_hotkey.modifiers) {
        return {ExitRequested{}};
    }

    // Check for Ctrl+1-9 quick selection (selects visible item by position)
    if (auto index_opt = get_ctrl_number_index(kbd_event);
        index_opt.has_value()) {
        const size_t visible_index = *index_opt;
        const size_t absolute_index =
            state.visible_range_offset + visible_index;

        if (absolute_index < state.items.size()) {
            state.selected_item_index = absolute_index;
            return {SelectionChanged{},
                    ActionRequested{state.items[absolute_index].command}};
        }
    }

    if (std::holds_alternative<ui::FileSearch>(state.mode)) {
        // Check for context menu action hotkeys on the selected file
        // (e.g., Ctrl+Enter for Open Containing Folder while in FileSearch)
        const auto selected_item = state.get_selected_item();
        if (selected_item && selected_item->path.has_value()) {
            const auto &path = selected_item->path.value();
            const auto file_actions = make_file_actions(path, config);

            for (const auto &action : file_actions) {
                if (action.hotkey.has_value() &&
                    hotkey_matches(kbd_event, *action.hotkey)) {
                    return {ActionRequested{action.command}};
                }
            }
        }
    } else {
        // Check for item hotkeys
        for (const auto &item : state.items) {
            if (item.hotkey.has_value() &&
                hotkey_matches(kbd_event, *item.hotkey)) {
                return {ActionRequested{item.command}};
            }
        }
    }

    switch (kbd_event.key) {
    case KeyCode::Escape:
        if (state.background_mode_active) {
            return {VisibilityToggleRequested{}};
        } else {
            return {ExitRequested{}};
        }
        break;

    case KeyCode::Up:
        // Normal item navigation first - move up through results
        if (!state.items.empty() && state.selected_item_index > 0) {
            state.selected_item_index--;
            return {SelectionChanged{}};
        }
        // History navigation in FileSearch mode when at top of results or
        // already navigating
        if (std::holds_alternative<FileSearch>(state.mode) &&
            (state.selected_item_index == 0 || state.items.empty() ||
             state.navigating_history)) {
            if (!state.file_search_history.empty()) {
                if (!state.navigating_history) {
                    // First time entering history - save current input
                    state.saved_input_buffer = state.input_buffer;
                    state.history_position = state.file_search_history.size();
                    state.navigating_history = true;
                }
                if (state.history_position > 0) {
                    state.history_position--;
                    state.input_buffer = std::string(
                        state.file_search_history.at(state.history_position));
                    state.cursor_position = state.input_buffer.size();
                    return {InputChanged{}};
                }
            }
        }
        break;

    case KeyCode::Down:
        // History navigation - move forward or exit
        if (state.navigating_history) {
            state.history_position++;
            if (state.history_position >= state.file_search_history.size()) {
                // Exit history, restore saved input
                state.input_buffer = state.saved_input_buffer;
                state.cursor_position = state.input_buffer.size();
                state.navigating_history = false;
                state.saved_input_buffer.clear();
                return {InputChanged{}};
            }
            state.input_buffer = std::string(
                state.file_search_history.at(state.history_position));
            state.cursor_position = state.input_buffer.size();
            return {InputChanged{}};
        }
        // Normal item navigation
        if (!state.items.empty()) {
            if (state.selected_item_index < state.items.size() - 1) {
                state.selected_item_index++;
            } else {
                state.selected_item_index = 0;
            }
            return {SelectionChanged{}};
        }
        break;

    case KeyCode::Tab:
        if (!std::holds_alternative<ContextMenu>(state.mode)) {
            if (try_open_context_menu(state, config)) {
                return {ContextMenuToggled{}};
            }
        }
        break;

    case KeyCode::Left:
        if (std::holds_alternative<ContextMenu>(state.mode)) {
            state.mode = FileSearch{.query = state.input_buffer};
            return {ContextMenuToggled{}};
        } else {
            if (state.cursor_position > 0) {
                state.cursor_position--;
                return {CursorPositionChanged{}};
            }
        }
        break;

    case KeyCode::Right:
        if (!std::holds_alternative<ContextMenu>(state.mode)) {
            if (state.cursor_position < state.input_buffer.size()) {
                // Cursor is not at end, just move it right
                state.cursor_position++;
                return {CursorPositionChanged{}};
            } else {
                // Cursor is at end, try to open context menu
                if (try_open_context_menu(state, config)) {
                    return {ContextMenuToggled{}};
                }
            }
        }
        break;

    case KeyCode::Home:
        if (!std::holds_alternative<ContextMenu>(state.mode)) {
            state.cursor_position = 0;
            return {CursorPositionChanged{}};
        }
        break;

    case KeyCode::End:
        if (!std::holds_alternative<ContextMenu>(state.mode)) {
            state.cursor_position = state.input_buffer.size();
            return {CursorPositionChanged{}};
        }
        break;

    case KeyCode::Return:
        if (auto item = state.get_selected_item()) {
            return {ActionRequested{item->command}};
        }
        break;

    case KeyCode::BackSpace:
        if (state.navigating_history) {
            state.navigating_history = false;
            state.saved_input_buffer.clear();
            state.history_position = state.file_search_history.size();
        }
        if (!state.input_buffer.empty() && state.cursor_position > 0) {
            state.input_buffer.erase(state.cursor_position - 1, 1);
            state.cursor_position--;
            return {InputChanged{}};
        }
        break;

    case KeyCode::Delete:
        if (!state.input_buffer.empty() &&
            state.cursor_position < state.input_buffer.size()) {
            state.input_buffer.erase(state.cursor_position, 1);
            return {InputChanged{}};
        }
        break;

    case KeyCode::Character:
        if (state.navigating_history) {
            state.navigating_history = false;
            state.saved_input_buffer.clear();
            state.history_position = state.file_search_history.size();
        }
        if (kbd_event.character >= 32 && kbd_event.character < 127) {
            state.input_buffer.insert(state.cursor_position, 1,
                                      *kbd_event.character);
            state.cursor_position++;
            return {InputChanged{}};
        }
        break;
    default:
        break;
    }
    return {};
}

std::vector<Event> handle_user_input(State &state, const UserInputEvent &input,
                                     const Config &config)
{
    std::vector<Event> events;

    std::visit(
        overloaded{
            [&](const KeyboardEvent &ev) {
                events = handle_keyboard_input(state, ev, config);
            },
            [&](const MousePositionEvent &ev) {
                // Perform hit testing
                auto item_index = window_pos_to_item_index(ev.position, state,
                                                           config.font_size);

                if (!item_index.has_value()) {
                    return;
                }
                // Update selection if hovering over a different
                // item
                if (state.selected_item_index != *item_index) {
                    state.selected_item_index = *item_index;
                    events.push_back(SelectionChanged{});
                }
            },
            [&](const MouseButtonEvent &ev) {
                if (!ev.pressed) {
                    return;
                }

                auto item_index = window_pos_to_item_index(ev.position, state,
                                                           config.font_size);

                if (!item_index.has_value() ||
                    *item_index >= state.items.size()) {
                    return;
                }

                // Update selection if clicking on a different item
                if (state.selected_item_index != *item_index) {
                    state.selected_item_index = *item_index;
                    events.push_back(SelectionChanged{});
                }

                if (ev.button == MouseButtonEvent::Button::Left) {
                    // Left-click: execute the action
                    events.push_back(
                        ActionRequested{state.items[*item_index].command});
                } else if (ev.button == MouseButtonEvent::Button::Right) {
                    // Right-click: open context menu (only in FileSearch mode)
                    if (try_open_context_menu(state, config)) {
                        events.push_back(ContextMenuToggled{});
                    }
                }
            },
            [&](const CursorEnterEvent &ev) {
                state.mouse_inside_window = true;

                // Optionally update selection on enter
                auto item_index = window_pos_to_item_index(ev.position, state,
                                                           config.font_size);

                if (!item_index.has_value()) {
                    return;
                }
                // Update selection if hovering over a different
                // item
                if (state.selected_item_index != *item_index) {
                    state.selected_item_index = *item_index;
                    events.push_back(SelectionChanged{});
                }
            },
            [&](const CursorLeaveEvent &) {
                state.mouse_inside_window = false;
            },
            [&](const MouseScrollEvent &ev) {
                // Scroll through items by moving the viewport
                if (state.items.empty()) {
                    return;
                }

                const size_t max_offset =
                    state.items.size() > state.max_visible_items
                        ? state.items.size() - state.max_visible_items
                        : 0;

                if (ev.direction == MouseScrollEvent::Direction::Up) {
                    // Scroll up - move viewport up
                    if (state.visible_range_offset > 0) {
                        state.visible_range_offset--;
                        events.push_back(ViewportChanged{});

                        // If selection is now below visible range, move it
                        if (state.selected_item_index >=
                            state.visible_range_offset +
                                state.max_visible_items) {
                            state.selected_item_index =
                                state.visible_range_offset +
                                state.max_visible_items - 1;
                            events.push_back(SelectionChanged{});
                        }
                    }
                } else {
                    // Scroll down - move viewport down
                    if (state.visible_range_offset < max_offset) {
                        state.visible_range_offset++;
                        events.push_back(ViewportChanged{});

                        // If selection is now above visible range, move it
                        if (state.selected_item_index <
                            state.visible_range_offset) {
                            state.selected_item_index =
                                state.visible_range_offset;
                            events.push_back(SelectionChanged{});
                        }
                    }
                }
            },
            [&](const HotkeyEvent &) {
                events.push_back(VisibilityToggleRequested{});
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

std::vector<Item>
convert_file_results_to_items(const std::vector<FileResult> &file_results)
{
    std::vector<Item> items;
    items.reserve(file_results.size());

    for (const auto &result : file_results) {
        try {
            const auto file_path = fs::canonical(result.path);

            if (fs::is_directory(file_path)) {
                items.push_back(Item{
                    .title = "📁 " + platform::path_to_string(file_path),
                    .description = serialize_file_info(file_path),
                    .path = file_path,
                    .command = OpenDirectory{.path = file_path},
                    .hotkey = std::nullopt,
                });
            } else {
                items.push_back(Item{
                    .title = "📄 " + platform::path_to_string(file_path),
                    .description = serialize_file_info(file_path),
                    .path = file_path,
                    .command = OpenFileCommand{.path = file_path},
                    .hotkey = std::nullopt,
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