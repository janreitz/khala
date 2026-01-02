#include "actions.h"
#include "config.h"
#include "utility.h"
#include "xwindow.h"

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

struct Item {
    std::string title;
    std::string description;
    Command command;
};

struct FileSearch {
    std::string query;
};
struct ContextMenu {
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
    size_t selected_item_index = 0;

    // Error display
    std::optional<std::string> error_message;

    // Progress tracking
    bool scan_complete = false;
    size_t total_files = 0;
    size_t processed_chunks = 0;

    Item get_selected_item() const;
    void set_error(const std::string &message);
    void clear_error();
    bool has_error() const;
};

enum class Event {
    NoEvent,
    InputChanged,
    SelectionChanged,
    CursorPositionChanged,
    ActionRequested,
    ContextMenuToggled,
    ExitRequested,
};

int calculate_actual_input_height(const Config &config, int screen_height);
int calculate_actual_item_height(const Config &config, int screen_height);

void draw(XWindow &window, const Config &config, const State &state);

} // namespace ui