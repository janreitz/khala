#pragma once

#include "actions.h"
#include "config.h"
#include "ranker.h"
#include "types.h"
#include "utility.h"

#include <optional>
#include <string>
#include <variant>

namespace ui
{

// UI layout constants
constexpr double BORDER_WIDTH = 4.0;
constexpr double ITEMS_SPACING = 8.0;
constexpr double CORNER_RADIUS = 4.0;
constexpr double TEXT_MARGIN = 15.0;
constexpr double INPUT_TEXT_MARGIN = 10.0;
constexpr double DESCRIPTION_SPACING = 10.0;
constexpr double INPUT_VERTICAL_PADDING = 12.0;  // Vertical padding for input area
constexpr double ITEM_VERTICAL_PADDING = 8.0;    // Vertical padding for each item

std::string format_file_count(size_t count);
std::string create_pagination_text(size_t visible_offset,
                                   size_t max_visible_items,
                                   size_t total_results,
                                   size_t total_available_results);
std::string create_highlighted_markup(const std::string &text,
                                      const std::vector<size_t> &match_positions);
int calculate_abs_input_height(int font_size);
int calculate_abs_item_height(int font_size);
size_t calculate_max_visible_items(unsigned int window_height, int font_size);
unsigned int calculate_window_height(int font_size, size_t item_count,
                                     size_t max_visible_items);

struct Item {
    std::string title;
    std::string description;
    std::optional<fs::path> path;
    Command command;
};

struct FileSearch {
    std::string query;
};
struct ContextMenu {
    std::string title;
    fs::path selected_file;
};
struct AppSearch {
    std::string query;
};
struct CommandSearch {
    std::string query;
};

using AppMode = std::variant<FileSearch, ContextMenu, AppSearch, CommandSearch>;

std::optional<std::string> get_query(const AppMode &mode);

struct State {
    std::string input_buffer;
    size_t cursor_position = 0;
    AppMode mode;

    // Results
    std::vector<Item> items;
    size_t visible_range_offset = 0;
    size_t selected_item_index = 0; // Absolute index in items
    size_t max_visible_items = 0;

    // Mouse state
    bool mouse_inside_window = false;

    // Error display
    std::optional<std::string> error_message;

    // Cache of last file search update for restoration when leaving ContextMenu
    // Also serves as the source of truth for progress tracking metadata
    std::optional<ResultUpdate> cached_file_search_update;

    Item get_selected_item() const;
    void set_error(const std::optional<std::string> &message);
    void clear_error();
    bool has_error() const;
};

struct InputChanged {
};
struct SelectionChanged {
};
struct CursorPositionChanged {
};
struct ActionRequested {
};
struct ContextMenuToggled {
};
struct ExitRequested {
};

using Event =
    std::variant<InputChanged, SelectionChanged, CursorPositionChanged,
                 ActionRequested, ContextMenuToggled, ExitRequested>;

std::optional<size_t> window_pos_to_item_index(
    const WindowCoord& position,
    const State& state,
    int font_size);

// Process keyboard events and update state, returning high-level events
std::vector<Event> handle_user_input(State &state, const UserInputEvent &input,
                                     const Config &config);

// Adjust visible_range_offset to keep selected_item_index visible
bool adjust_visible_range(State &state, size_t max_visible_items);
size_t required_item_count(const State &state, size_t max_visible_items);

// Convert FileResults from ranker to UI Items
std::vector<Item> convert_file_results_to_items(
    const std::vector<FileResult> &file_results);

} // namespace ui