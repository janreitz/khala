// config.cpp
#include "config.h"
#include "logger.h"
#include "utility.h"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{

std::multimap<std::string, std::string> parse_ini(const fs::path &path)
{
    std::multimap<std::string, std::string> result;
    std::ifstream file(path);
    std::string line;

    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#')
            continue;

        auto pos = line.find('=');
        if (pos != std::string::npos) {
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);
            result.emplace(key, value);
        }
    }
    return result;
}

std::optional<std::string>
get_last(const std::multimap<std::string, std::string> &map,
         const std::string &key)
{
    auto range = map.equal_range(key);
    if (range.first == range.second)
        return std::nullopt;

    auto last = std::prev(range.second);
    return last->second;
}

std::vector<std::string>
get_all(const std::multimap<std::string, std::string> &map,
        const std::string &key)
{
    std::vector<std::string> result;
    auto range = map.equal_range(key);

    for (auto it = range.first; it != range.second; ++it) {
        result.push_back(it->second);
    }

    return result;
}

int get_int_or(const std::multimap<std::string, std::string> &map,
               const std::string &key, int default_value)
{
    auto value = get_last(map, key);
    if (!value)
        return default_value;

    try {
        return std::stoi(*value);
    } catch (...) {
        return default_value;
    }
}

bool get_bool_or(const std::multimap<std::string, std::string> &map,
                 const std::string &key, bool default_value)
{
    auto value = get_last(map, key);
    if (!value)
        return default_value;

    return *value == "true" || *value == "1";
}

std::string get_string_or(const std::multimap<std::string, std::string> &map,
                          const std::string &key, std::string default_value)
{
    auto value = get_last(map, key);
    return value ? *value : default_value;
}

double get_double_or(const std::multimap<std::string, std::string> &map,
                     const std::string &key, double default_value)
{
    auto value = get_last(map, key);
    if (!value)
        return default_value;

    try {
        return std::stod(*value);
    } catch (...) {
        return default_value;
    }
}

ActionType get_action_type_or(const std::multimap<std::string, std::string> &map,
                               const std::string &key,
                               ActionType default_value)
{
    auto value = get_last(map, key);
    if (!value)
        return default_value;

    if (*value == "file") {
        return ActionType::File;
    } else if (*value == "directory") {
        return ActionType::Directory;
    } else if (*value == "utility") {
        return ActionType::Utility;
    }

    return default_value;
}

std::set<fs::path>
get_dirs_or(const std::multimap<std::string, std::string> &map,
            const std::string &key, std::set<fs::path> default_value,
            std::vector<std::string> &warnings)
{
    auto values = get_all(map, key);
    if (values.empty()) {
        return default_value;
    }

    std::set<fs::path> result;
    for (const auto &value : values) {
        fs::path dir_path(value);
        if (!fs::exists(dir_path)) {
            warnings.push_back("Config: " + key +
                               " path does not exist: " + value);
            continue;
        }
        if (!fs::is_directory(dir_path)) {
            warnings.push_back("Config: " + key +
                               " path is not a directory: " + value);
            continue;
        }
        result.insert(fs::canonical(dir_path));
    }

    return result.empty() ? default_value : result;
}

std::set<std::string>
get_strings_or(const std::multimap<std::string, std::string> &map,
               const std::string &key, std::set<std::string> default_value)
{
    std::set<std::string> result = default_value;
    auto values = get_all(map, key);

    for (const auto &value : values) {
        if (!value.empty()) {
            result.insert(value);
        }
    }

    return result;
}

Color get_color_or(const std::multimap<std::string, std::string> &map,
                   const std::string &key, Color default_value)
{
    auto value = get_last(map, key);
    if (!value) {
        return default_value;
    }
    auto parsed = parse_color(*value);
    return parsed ? *parsed : default_value;
}

} // namespace

uint16_t Color::pango_red() const { return static_cast<uint16_t>(r * 65535); }
uint16_t Color::pango_green() const { return static_cast<uint16_t>(g * 65535); }
uint16_t Color::pango_blue() const { return static_cast<uint16_t>(b * 65535); }

// Parse hex color: "#RGB", "#RGBA", "#RRGGBB", or "#RRGGBBAA"
std::optional<Color> parse_color(const std::string &str)
{
    if (str.empty() || str[0] != '#') {
        return std::nullopt;
    }

    std::string hex = str.substr(1);

    // Expand shorthand: #RGB -> #RRGGBB, #RGBA -> #RRGGBBAA
    if (hex.length() == 3 || hex.length() == 4) {
        std::string expanded;
        for (char c : hex) {
            expanded += c;
            expanded += c;
        }
        hex = expanded;
    }

    if (hex.length() != 6 && hex.length() != 8) {
        return std::nullopt;
    }

    auto parse_component = [](const std::string &s,
                              size_t offset) -> std::optional<double> {
        unsigned int val = 0;
        auto [ptr, ec] =
            std::from_chars(s.data() + offset, s.data() + offset + 2, val, 16);
        if (ec != std::errc{}) {
            return std::nullopt;
        }
        return val / 255.0;
    };

    auto r = parse_component(hex, 0);
    auto g = parse_component(hex, 2);
    auto b = parse_component(hex, 4);

    if (!r || !g || !b) {
        return std::nullopt;
    }

    Color color{*r, *g, *b, 1.0};

    if (hex.length() == 8) {
        auto a = parse_component(hex, 6);
        if (!a) {
            return std::nullopt;
        }
        color.a = *a;
    }

    return color;
}

std::optional<ui::KeyboardEvent> parse_hotkey(const std::string &hotkey_str)
{
    ui::KeyboardEvent result;

    // Split by '+' and parse each part
    std::string remaining = hotkey_str;
    while (!remaining.empty()) {
        size_t plus_pos = remaining.find('+');
        std::string part;
        if (plus_pos != std::string::npos) {
            part = remaining.substr(0, plus_pos);
            remaining = remaining.substr(plus_pos + 1);
        } else {
            part = remaining;
            remaining.clear();
        }

        // Trim whitespace
        while (!part.empty() && part.front() == ' ')
            part.erase(part.begin());
        while (!part.empty() && part.back() == ' ')
            part.pop_back();

        // Convert to lowercase for comparison
        std::string lower;
        for (char c : part) {
            lower += static_cast<char>(tolower(static_cast<unsigned char>(c)));
        }

        if (lower == "ctrl" || lower == "control") {
            result.modifiers |= ui::KeyModifier::Ctrl;
        } else if (lower == "alt") {
            result.modifiers |= ui::KeyModifier::Alt;
        } else if (lower == "shift") {
            result.modifiers |= ui::KeyModifier::Shift;
        } else if (lower == "super" || lower == "win" || lower == "meta") {
            result.modifiers |= ui::KeyModifier::Super;
        } else if (lower == "space") {
            result.key = ui::KeyCode::Space;
        } else if (lower == "return" || lower == "enter") {
            result.key = ui::KeyCode::Return;
        } else if (lower == "tab") {
            result.key = ui::KeyCode::Tab;
        } else if (lower == "escape" || lower == "esc") {
            result.key = ui::KeyCode::Escape;
        } else if (lower.length() == 1 && lower[0] >= 'a' && lower[0] <= 'z') {
            // Single letter key (A-Z)
            result.key = static_cast<ui::KeyCode>(
                static_cast<int>(ui::KeyCode::A) + (lower[0] - 'a'));
        } else if (lower.length() == 1 && lower[0] >= '0' && lower[0] <= '9') {
            // Number key
            result.key = static_cast<ui::KeyCode>(
                static_cast<int>(ui::KeyCode::Num0) + (lower[0] - '0'));
        } else if (lower.length() == 2 && lower[0] == 'f' && lower[1] >= '1' &&
                   lower[1] <= '9') {
            // F1-F9
            result.key = static_cast<ui::KeyCode>(
                static_cast<int>(ui::KeyCode::F1) + (lower[1] - '1'));
        } else if (lower.length() == 3 && lower[0] == 'f' && lower[1] == '1' &&
                   lower[2] >= '0' && lower[2] <= '2') {
            // F10-F12
            result.key = static_cast<ui::KeyCode>(
                static_cast<int>(ui::KeyCode::F10) + (lower[2] - '0'));
        } else {
            // Unknown key
            return std::nullopt;
        }
    }

    // Must have a valid key
    if (result.key == ui::KeyCode::NoKey) {
        return std::nullopt;
    }

    return result;
}

std::optional<ui::KeyboardEvent>
get_hotkey(const std::multimap<std::string, std::string> &map,
              const std::string &key)
{
    auto value = get_last(map, key);
    if (!value) {
        return std::nullopt;
    }
    return parse_hotkey(*value);
}

// Check if hotkey is a reserved navigation key or Ctrl+0-9
bool is_reserved_hotkey(const ui::KeyboardEvent &hotkey)
{
    using ui::KeyCode;
    using ui::KeyModifier;

    // TODO normal characters without modifier are also reserved for regular input.

    // Navigation keys without modifiers are reserved
    if (hotkey.modifiers == KeyModifier::NoModifier) {
        switch (hotkey.key) {
        case KeyCode::Up:
        case KeyCode::Down:
        case KeyCode::Left:
        case KeyCode::Right:
        case KeyCode::Tab:
        case KeyCode::Escape:
        case KeyCode::Return:
        case KeyCode::Home:
        case KeyCode::End:
        case KeyCode::BackSpace:
        case KeyCode::Delete:
            return true;
        default:
            break;
        }
    }

    // Ctrl+0 through Ctrl+9 are reserved for quick selection
    if (hotkey.modifiers == KeyModifier::Ctrl) {
        if (hotkey.key >= KeyCode::Num0 && hotkey.key <= KeyCode::Num9) {
            return true;
        }
    }

    return false;
}

// Returns the name of the hardcoded shortcut if it conflicts, empty string otherwise
std::string get_hardcoded_conflict(const ui::KeyboardEvent &hotkey)
{
    using ui::KeyCode;
    using ui::KeyModifier;

    // Ctrl+C - Copy Path to Clipboard
    if (hotkey.key == KeyCode::C && hotkey.modifiers == KeyModifier::Ctrl) {
        return "Copy Path to Clipboard";
    }

    // Ctrl+Shift+C - Copy Content to Clipboard
    if (hotkey.key == KeyCode::C &&
        hotkey.modifiers == (KeyModifier::Ctrl | KeyModifier::Shift)) {
        return "Copy Content to Clipboard";
    }

    // Ctrl+Return - Open Containing Folder
    if (hotkey.key == KeyCode::Return && hotkey.modifiers == KeyModifier::Ctrl) {
        return "Open Containing Folder";
    }

    return "";
}

// Compare two KeyboardEvent structs for equality
bool hotkeys_match(const ui::KeyboardEvent &a, const ui::KeyboardEvent &b)
{
    return a.key == b.key && a.modifiers == b.modifiers;
}

std::set<fs::path> Config::default_index_roots()
{
    const auto home_dir = platform::get_home_dir();
    return {home_dir.value_or(".")};
}

fs::path Config::default_path()
{
#ifdef PLATFORM_WIN32
    const char *appdata = std::getenv("APPDATA");
    return fs::path(appdata) / "khala" / "config.ini";
#else
    const auto home_dir = platform::get_home_dir();
    return home_dir.value_or(".") / ".config" / "khala" / "config.ini";
#endif
}

void load_theme(const std::string &theme_name,
                const std::vector<fs::path> &theme_dirs, Config &config)
{
    for (const auto &theme_dir : theme_dirs) {
        if (!fs::exists(theme_dir)) {
            continue;
        }

        const fs::path theme_file = theme_dir / (theme_name + ".ini");
        if (!fs::exists(theme_file)) {
            continue;
        }

        auto map = parse_ini(theme_file);

        // Apply theme colors
        config.input_background_color = get_color_or(
            map, "input_background_color", config.input_background_color);
        config.background_color =
            get_color_or(map, "background_color", config.background_color);
        config.border_color =
            get_color_or(map, "border_color", config.border_color);
        config.text_color = get_color_or(map, "text_color", config.text_color);
        config.selection_color =
            get_color_or(map, "selection_color", config.selection_color);
        config.selection_text_color = get_color_or(map, "selection_text_color",
                                                   config.selection_text_color);
        config.description_color =
            get_color_or(map, "description_color", config.description_color);
        config.selection_description_color =
            get_color_or(map, "selection_description_color",
                         config.selection_description_color);

        LOG_INFO("Loaded theme '%s' from %s", theme_name.c_str(),
                 platform::path_to_string(theme_file).c_str());
        return; // Found and loaded, return immediately
    }

    LOG_WARNING("Theme '%s' not found, using built-in defaults",
                theme_name.c_str());
}

ConfigLoadResult load_config(const fs::path &path)
{
    Config cfg;
    std::vector<std::string> warnings;
    cfg.config_path = path;

    if (!fs::exists(path)) {
        fs::create_directories(path.parent_path());
        cfg.save(path);
        return {.config = cfg, .warnings = {}};
    }

    auto map = parse_ini(path);

    // Window positioning and sizing
    cfg.width_ratio = get_double_or(map, "width_ratio", cfg.width_ratio);
    cfg.height_ratio = get_double_or(map, "height_ratio", cfg.height_ratio);
    cfg.x_position = get_double_or(map, "x_position", cfg.x_position);
    cfg.y_position = get_double_or(map, "y_position", cfg.y_position);
    // Appearance
    cfg.font_name = get_string_or(map, "font_name", cfg.font_name);
    cfg.font_size = get_int_or(map, "font_size", cfg.font_size);
    cfg.theme = get_string_or(map, "theme", cfg.theme);
    load_theme(
        cfg.theme,
        {fs::path(KHALA_INSTALL_DIR) / "themes", path.parent_path() / "themes"},
        cfg);

    // Behavior
    cfg.quit_on_action = get_bool_or(map, "quit_on_action", cfg.quit_on_action);
    cfg.editor = get_string_or(map, "editor", cfg.editor);
    cfg.file_manager = get_string_or(map, "file_manager", cfg.file_manager);
    cfg.default_shell = get_string_or(map, "default_shell", cfg.default_shell);

    // Background mode
    cfg.background_mode =
        get_bool_or(map, "background_mode", cfg.background_mode);
    cfg.hotkey = get_hotkey(map, "hotkey").value_or(cfg.hotkey);
    cfg.quit_hotkey = get_hotkey(map, "quit_hotkey").value_or(cfg.quit_hotkey);

    // Indexing
    cfg.index_roots = get_dirs_or(map, "index_root", cfg.index_roots, warnings);
    cfg.ignore_dirs = get_dirs_or(map, "ignore_dir", cfg.ignore_dirs, warnings);
    cfg.ignore_dir_names =
        get_strings_or(map, "ignore_dir_name", cfg.ignore_dir_names);

    std::vector<fs::path> commands_dirs{fs::path(KHALA_INSTALL_DIR) /
                                            "commands",
                                        path.parent_path() / "commands"};
    std::unordered_map<std::string, CustomActionDef> actions_by_stem;
    for (const auto &commands_dir : commands_dirs) {
        if (fs::exists(commands_dir)) {
            for (const auto &entry : fs::directory_iterator(commands_dir)) {
                if (entry.path().extension() != ".ini")
                    continue;

                auto command_map = parse_ini(entry.path());

                std::string title = get_string_or(command_map, "title", "");
                std::string description =
                    get_string_or(command_map, "description", "");
                std::string shell_cmd =
                    get_string_or(command_map, "shell_cmd", "");
                ActionType action_type =
                    get_action_type_or(command_map, "action_type", ActionType::Utility);
                bool stdout_to_clipboard =
                    get_bool_or(command_map, "stdout_to_clipboard", false);
                std::string shell = get_string_or(command_map, "shell", "");
                // TODO use get_hotkey_or
                std::optional<ui::KeyboardEvent> hotkey = get_hotkey(command_map, "hotkey");

                if (title.empty() || shell_cmd.empty()) {
                    continue;
                }

                actions_by_stem.insert_or_assign(
                    platform::path_to_string(entry.path().stem()),
                    CustomActionDef{
                        .title = title,
                        .description = description,
                        .shell_cmd = shell_cmd,
                        .action_type = action_type,
                        .stdout_to_clipboard = stdout_to_clipboard,
                        .shell = shell.empty() ? std::nullopt
                                               : std::optional<std::string>(shell),
                        .hotkey = hotkey,
                    });
            }
        }
    }

    auto values = actions_by_stem | std::views::values;
    cfg.custom_actions.assign(values.begin(), values.end());

    // Validate hotkeys and check for conflicts
    std::vector<std::pair<std::string, ui::KeyboardEvent>> used_hotkeys;
    for (auto &action : cfg.custom_actions) {
        if (!action.hotkey)
            continue;

        const auto &hk = *action.hotkey;

        // Check if reserved
        if (is_reserved_hotkey(hk)) {
            LOG_WARNING("Hotkey for '%s' is reserved (navigation/quick-select), ignoring",
                        action.title.c_str());
            action.hotkey = std::nullopt;
            continue;
        }

        // Check if it conflicts with global hotkey or quit_hotkey
        if (hotkeys_match(hk, cfg.hotkey)) {
            LOG_WARNING("Hotkey for '%s' conflicts with global hotkey, ignoring",
                        action.title.c_str());
            action.hotkey = std::nullopt;
            continue;
        }
        if (hotkeys_match(hk, cfg.quit_hotkey)) {
            LOG_WARNING("Hotkey for '%s' conflicts with quit hotkey, ignoring",
                        action.title.c_str());
            action.hotkey = std::nullopt;
            continue;
        }

        // Check if it conflicts with hardcoded shortcuts (warn but allow)
        std::string hardcoded_conflict = get_hardcoded_conflict(hk);
        if (!hardcoded_conflict.empty()) {
            LOG_WARNING("Hotkey for '%s' overrides hardcoded '%s'",
                        action.title.c_str(), hardcoded_conflict.c_str());
        }

        // Check for duplicate custom command hotkeys
        bool duplicate = false;
        for (const auto &[existing_title, existing_hk] : used_hotkeys) {
            if (hotkeys_match(hk, existing_hk)) {
                LOG_WARNING("Hotkey for '%s' duplicates '%s', ignoring",
                            action.title.c_str(), existing_title.c_str());
                action.hotkey = std::nullopt;
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            used_hotkeys.emplace_back(action.title, hk);
        }
    }
    return {.config = cfg, .warnings = std::move(warnings)};
}

void Config::save(const fs::path &path) const
{
    std::ofstream file(path);

    file << "# Khala Launcher Configuration\n";
    file << "# This file is auto-generated with defaults on first run.\n";
    file << "\n";

    file << "# Window positioning and sizing (as percentages 0.0-1.0)\n";
    file << "width_ratio=" << width_ratio << "\n";
    file << "height_ratio=" << height_ratio << "\n";
    file << "x_position=" << x_position << "\n";
    file << "y_position=" << y_position << "\n";
    file << "\n";

    file << "# Appearance\n";
    file << "# Available themes: default-light, default-dark, "
            "tomorrow-night-eighties,\n";
    file << "#                   gruvbox-dark, nord, solarized-light\n";
    file << "# Custom themes can be placed in ~/.khala/themes/\n";
    file << "theme=" << theme << "\n";
    file << "font_name=" << font_name << "\n";
    file << "font_size=" << font_size << "\n";
    file << "\n";

    file << "# Behavior\n";
    file << "quit_on_action=" << (quit_on_action ? "true" : "false") << "\n";
    file << "editor=" << editor << "\n";
    file << "file_manager=" << file_manager << "\n";
    file << "default_shell=" << default_shell << "\n";
    file << "\n";

    file << "# Background mode (Windows)\n";
    file << "# When enabled, app starts hidden and registers a global hotkey\n";
    file << "background_mode=" << (background_mode ? "true" : "false") << "\n";
    file << "# Hotkey format: modifier keys + key (e.g., Alt+Space, "
            "Ctrl+Shift+K)\n";
    file << "hotkey=" << to_string(hotkey) << "\n";
    file << "# Hotkey to quit the application (In background mode, Esc only "
            "hides)\n";
    file << "quit_hotkey=" << to_string(quit_hotkey) << "\n";
    file << "\n";

    file << "# Indexing \n";
    file << "# Multiple index_root entries can be specified for indexing "
            "multiple locations\n";
    for (const auto &root : index_roots) {
        file << "index_root=" << platform::path_to_string(fs::canonical(root))
             << "\n";
    }
    for (const auto &dir : ignore_dirs) {
        file << "ignore_dir=" << platform::path_to_string(fs::canonical(dir))
             << "\n";
    }
    for (const auto &dir_name : ignore_dir_names) {
        file << "ignore_dir_name=" << dir_name << "\n";
    }
    file << "\n";

    file.flush();

    LOG_INFO("Written config to %s",
             platform::path_to_string(fs::canonical(path)).c_str());
}