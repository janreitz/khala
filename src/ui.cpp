#include "ui.h"
#include "actions.h"
#include "config.h"
#include "logger.h"
#include "ranker.h"
#include "streamingindex.h"
#include "types.h"
#include "utility.h"
#include "vector.h"

#include <algorithm>
#include <cassert>
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
    const auto visible_item_index =
        static_cast<size_t>(relative_y / item_height);

    // Convert to absolute index
    const size_t absolute_index =
        state.visible_range_offset + visible_item_index;

    // Validate bounds
    if (absolute_index >= state.items.count) {
        return std::nullopt;
    }

    // Check if we're within the visible range
    if (visible_item_index >= state.max_visible_items) {
        return std::nullopt;
    }

    return absolute_index;
}

bool State::has_selected_item() const
{
    return selected_item_index < items.count;
}

void State::push_error(const std::string &error)
{
    if (mode != AppMode::Error) {
        mode = AppMode::Error;
        vec_for_each_mut(&items, ui_item_free, NULL);
        vec_clear(&items);
    }
    selected_item_index = std::numeric_limits<size_t>::max();

    const ui::Item error_item{
        .title = str_from_std_string(std::string("⚠ ") + error),
        .description = str_from_literal(""),
        .path_idx = NO_PATH_INDEX,
        .command = {.type = CMD_NOOP, .path_idx = NO_PATH_INDEX},
        .hotkey = std::nullopt,
    };
    vec_push(&items, &error_item);
}

bool State::has_errors() const
{
    return mode == AppMode::Error;
}

void State::clear_errors()
{
    mode = AppMode::FileSearch;
}

void ui_item_init(Item *item)
{
    item->title = {.data = nullptr, .len = 0, .cap = 0};
    item->description = {.data = nullptr, .len = 0, .cap = 0};
    item->path_idx = NO_PATH_INDEX;
    item->command = {.type = CMD_NOOP, .path_idx = NO_PATH_INDEX};
    item->hotkey = std::nullopt;
}

bool ui_item_free(void *item, void *)
{
    auto *it = static_cast<Item *>(item);
    str_free(&it->title);
    str_free(&it->description);
    it->path_idx = NO_PATH_INDEX;
    cmd_free(&it->command);
    it->hotkey = std::nullopt;
    return true;
}

bool ui_item_collect(const void *item_ptr, void *user_data)
{
    const Item *item = static_cast<const Item *>(item_ptr);
    Vec *vec = static_cast<Vec *>(user_data);

    // Create a temporary item with zeroed memory to avoid freeing garbage
    Item temp_item;
    ui_item_init(&temp_item);

    if (!ui_item_copy(&temp_item, item)) {
        return false;
    }

    if (vec_push(vec, &temp_item) != 0) {
        // Push failed, clean up the copied item
        ui_item_free(&temp_item, nullptr);
        return false;
    }

    // CRITICAL: Neutralize temp_item's heap-owning members to prevent
    // double-free. vec_push did a memcpy, so temp_item and the Vec entry now
    // share pointers.
    temp_item.title.data = nullptr;
    temp_item.description.data = nullptr;
    temp_item.command.type = CMD_NOOP;

    return true;
}

bool ui_item_copy(Item *dst, const Item *src)
{
    Str temp_title = {nullptr, 0, 0};
    Str temp_desc = {nullptr, 0, 0};

    if (!str_copy(&temp_title, &src->title)) {
        return false;
    }
    if (!str_copy(&temp_desc, &src->description)) {
        str_free(&temp_title);
        return false;
    }

    // Free old dst resources
    str_free(&dst->title);
    str_free(&dst->description);
    cmd_free(&dst->command);

    dst->title = temp_title;
    dst->description = temp_desc;
    dst->path_idx = src->path_idx;
    cmd_copy(&dst->command, &src->command);
    dst->hotkey = src->hotkey;

    return true;
}

static bool ui_item_hotkey_matches(const void *item, const void *user_data)
{
    const auto *_item = static_cast<const Item *>(item);
    const auto *kbd_event = static_cast<const KeyboardEvent *>(user_data);
    return _item->hotkey.has_value() &&
           hotkey_matches(*kbd_event, *_item->hotkey);
}

// Try to open context menu for the currently selected file item
// Returns true if context menu was opened, false otherwise
static bool try_open_context_menu(State &state, const Config &config)
{
    // Only open context menu in FileSearch mode
    if (state.mode != AppMode::FileSearch) {
        return false;
    }

    // Need items to show context menu for
    if (state.items.count == 0) {
        return false;
    }

    if (!state.has_selected_item()) {
        return false;
    }

    const auto *selected_item = static_cast<const Item *>(
        vec_at(&state.items, state.selected_item_index));
    if (selected_item->path_idx == NO_PATH_INDEX) {
        return false;
    }

    // Resolve path from streaming index
    assert(state.index != nullptr);
    const auto path_sv = state.index->at(selected_item->path_idx);
    const fs::path file_path{std::string{path_sv.data, path_sv.len}};

    // Compose title: emoji + path (same as render time)
    const char *emoji =
        selected_item->command.type == CMD_OPEN_DIRECTORY ? "📁 " : "📄 ";
    const std::string title = std::string(emoji) +
                              platform::path_to_string(file_path);

    state.mode = AppMode::ContextMenu;
    state.context_menu_title = title;
    state.selected_item_index = 0;
    vec_for_each_mut(&state.items, ui_item_free, NULL);
    vec_clear(&state.items);
    for_each_file_action(file_path, selected_item->path_idx, config,
                         ui_item_collect, &state.items);
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

typedef struct {
    const KeyboardEvent *kbd_event;
    Command *matched_command; // out: set if found
    bool found;
} HotkeyMatchContext;

bool find_matching_hotkey(const void *item, void *user_data)
{
    auto item_ = static_cast<const Item *>(item);
    auto *ctx = static_cast<HotkeyMatchContext *>(user_data);
    if (item_->hotkey && hotkey_matches(*(ctx->kbd_event), *(item_->hotkey))) {
        *ctx->matched_command = item_->command;
        ctx->found = true;
        return false; // stop iteration
    }
    return true; // continue
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

        if (absolute_index < state.items.count) {
            state.selected_item_index = absolute_index;
            const auto *item =
                static_cast<const Item *>(vec_at(&state.items, absolute_index));
            return {SelectionChanged{},
                    ActionRequested{.command = item->command}};
        }
    }

    if (state.mode == AppMode::FileSearch) {
        // Check for context menu action hotkeys on the selected file
        // (e.g., Ctrl+Enter for Open Containing Folder while in FileSearch)
        if (state.has_selected_item()) {
            const auto *selected_item = static_cast<const Item *>(
                vec_at(&state.items, state.selected_item_index));
            if (selected_item->path_idx != NO_PATH_INDEX) {
                assert(state.index != nullptr);
                const auto path_sv =
                    state.index->at(selected_item->path_idx);
                const fs::path path{std::string{path_sv.data, path_sv.len}};
                Command matched = {.type = CMD_NOOP, .path_idx = NO_PATH_INDEX};
                HotkeyMatchContext ctx = {.kbd_event = &kbd_event,
                                          .matched_command = &matched,
                                          .found = false};
                for_each_file_action(path, selected_item->path_idx, config,
                                     find_matching_hotkey, &ctx);
                if (ctx.found) {
                    return {ActionRequested{.command = matched}};
                }
            }
        }
    } else {
        // Check for item hotkeys
        const auto *matching_item = static_cast<const Item *>(
            vec_find_if(&state.items, ui_item_hotkey_matches, &kbd_event));
        if (matching_item != NULL) {
            return {ActionRequested{.command = matching_item->command}};
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
        if (!state.items.count == 0 && state.selected_item_index > 0) {
            state.selected_item_index--;
            return {SelectionChanged{}};
        }
        // History navigation in FileSearch mode when at top of results or
        // already navigating
        if (state.mode == AppMode::FileSearch &&
            (state.selected_item_index == 0 || state.items.count == 0 ||
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
                    const auto sv = state.file_search_history.at(state.history_position);
                    state.input_buffer = std::string(sv.data, sv.len);
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
            const auto sv = state.file_search_history.at(state.history_position);
            state.input_buffer = std::string(sv.data, sv.len);
            state.cursor_position = state.input_buffer.size();
            return {InputChanged{}};
        }
        // Normal item navigation
        if (!state.items.count == 0) {
            if (state.selected_item_index < state.items.count - 1) {
                state.selected_item_index++;
            } else {
                state.selected_item_index = 0;
            }
            return {SelectionChanged{}};
        }
        break;

    case KeyCode::Tab:
        if (state.mode != AppMode::ContextMenu) {
            if (try_open_context_menu(state, config)) {
                return {ContextMenuToggled{}};
            }
        }
        break;

    case KeyCode::Left:
        if (state.mode == AppMode::ContextMenu) {
            state.mode = AppMode::FileSearch;
            return {ContextMenuToggled{}};
        } else {
            if (state.cursor_position > 0) {
                state.cursor_position--;
                return {CursorPositionChanged{}};
            }
        }
        break;

    case KeyCode::Right:
        if (state.mode != AppMode::ContextMenu) {
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
        if (state.mode != AppMode::ContextMenu) {
            state.cursor_position = 0;
            return {CursorPositionChanged{}};
        }
        break;

    case KeyCode::End:
        if (state.mode != AppMode::ContextMenu) {
            state.cursor_position = state.input_buffer.size();
            return {CursorPositionChanged{}};
        }
        break;

    case KeyCode::Return:
        if (state.has_selected_item()) {
            const auto *item = static_cast<const Item *>(
                vec_at(&state.items, state.selected_item_index));
            return {ActionRequested{.command = item->command}};
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
                    *item_index >= state.items.count) {
                    return;
                }

                // Update selection if clicking on a different item
                if (state.selected_item_index != *item_index) {
                    state.selected_item_index = *item_index;
                    events.push_back(SelectionChanged{});
                }

                if (ev.button == MouseButtonEvent::Button::Left) {
                    // Left-click: execute the action
                    const auto *clicked_item = static_cast<const Item *>(
                        vec_at(&state.items, *item_index));
                    events.push_back(
                        ActionRequested{.command = clicked_item->command});
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
                if (state.items.count == 0) {
                    return;
                }

                const size_t max_offset =
                    state.items.count > state.max_visible_items
                        ? state.items.count - state.max_visible_items
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
    if (state.items.count == 0) {
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

bool populate_file_item(const StreamingIndex &index, const RankResult &result,
                        Item *out_item)
{
    try {
        const auto path_sv = index.at(result.index);
        const fs::path file_path{std::string{path_sv.data, path_sv.len}};
        const bool is_dir = fs::is_directory(file_path);

        out_item->title = {nullptr, 0, 0}; // Composed at render time
        out_item->description =
            str_from_std_string(serialize_file_info(file_path));
        out_item->path_idx = result.index;
        out_item->command = {.type = is_dir ? CMD_OPEN_DIRECTORY : CMD_OPEN_FILE,
                             .path_idx = result.index};
        out_item->hotkey = std::nullopt;
        return true;
    } catch (const std::exception &e) {
        LOG_WARNING("Could not stat path at index %zu: %s", result.index,
                    e.what());
        return false;
    }
}

} // namespace ui