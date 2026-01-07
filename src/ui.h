#pragma once

#include "actions.h"
#include "config.h"
#include "ranker.h"
#include "types.h"
#include "utility.h"
#include "window.h"

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

int calculate_abs_input_height(int font_size);
int calculate_abs_item_height(int font_size);
size_t calculate_max_visible_items(int window_height, int font_size);
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

// Process keyboard events and update state, returning high-level events
std::vector<Event> handle_user_input(State &state, const UserInputEvent &input,
                                     const Config &config);

// Adjust visible_range_offset to keep selected_item_index visible
bool adjust_visible_range(State &state, size_t max_visible_items);
size_t required_item_count(const State &state, size_t max_visible_items);

// Convert FileResults from ranker to UI Items
std::vector<Item> convert_file_results_to_items(
    const std::vector<FileResult> &file_results);

void draw(cairo_t *cr, int window_width, int window_height,
          const Config &config, const State &state);

} // namespace ui