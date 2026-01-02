// config.cpp
#include "config.h"

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

size_t get_size_or(const std::multimap<std::string, std::string> &map,
                   const std::string &key, size_t default_value)
{
    auto value = get_last(map, key);
    if (!value)
        return default_value;

    try {
        return std::stoul(*value);
    } catch (...) {
        return default_value;
    }
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

fs::path get_dir_or(const std::multimap<std::string, std::string> &map,
                    const std::string &key, fs::path default_value)
{
    auto value = get_last(map, key);
    if (!value)
        return default_value;

    const fs::path path(*value);
    if (!fs::exists(path) || !fs::is_directory(path)) {
        return default_value;
    }

    return fs::canonical(path);
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

std::set<fs::path>
get_dirs_or(const std::multimap<std::string, std::string> &map,
            const std::string &key, std::set<fs::path> default_value)
{
    std::set<fs::path> result = default_value;
    auto values = get_all(map, key);

    for (const auto &value : values) {
        fs::path dir_path(value);
        if (fs::exists(dir_path) && fs::is_directory(dir_path)) {
            result.insert(fs::canonical(dir_path));
        }
    }

    return result;
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

fs::path Config::default_path()
{
    const char *home = std::getenv("HOME");
    if (!home)
        return {};
    return fs::path(home) / ".khala" / "config.ini";
}

void load_theme(const std::string &theme_name,
                const std::vector<fs::path> &theme_dirs, Config &config)
{
    for (const auto &theme_dir : theme_dirs) {
        if (!fs::exists(theme_dir)) {
            continue;
        }

        fs::path theme_file = theme_dir / (theme_name + ".ini");
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

        printf("Loaded theme '%s' from %s\n", theme_name.c_str(),
               theme_file.generic_string().c_str());
        return; // Found and loaded, return immediately
    }

    printf("Warning: Theme '%s' not found, using built-in defaults\n",
           theme_name.c_str());
}

Config Config::load(const fs::path &path)
{
    Config cfg;
    cfg.config_path = path;

    if (!fs::exists(path)) {
        fs::create_directories(path.parent_path());
        cfg.save(path);
        return cfg;
    }

    auto map = parse_ini(path);

    // Window positioning and sizing
    cfg.width_ratio = get_double_or(map, "width_ratio", cfg.width_ratio);
    cfg.x_position = get_double_or(map, "x_position", cfg.x_position);
    cfg.y_position = get_double_or(map, "y_position", cfg.y_position);
    cfg.input_height_ratio =
        get_double_or(map, "input_height_ratio", cfg.input_height_ratio);
    cfg.item_height_ratio =
        get_double_or(map, "item_height_ratio", cfg.item_height_ratio);
    cfg.max_visible_items =
        get_size_or(map, "max_visible_items", cfg.max_visible_items);

    // Appearance
    cfg.font_name = get_string_or(map, "font_name", cfg.font_name);
    cfg.font_size = get_int_or(map, "font_size", cfg.font_size);
    cfg.theme = get_string_or(map, "theme", cfg.theme);
    load_theme(
        cfg.theme,
        {fs::path(KHALA_DATADIR) / "themes", path.parent_path() / "themes"},
        cfg);

    // Behavior
    cfg.quit_on_action = get_bool_or(map, "quit_on_action", cfg.quit_on_action);
    cfg.editor = get_string_or(map, "editor", cfg.editor);
    cfg.file_manager = get_string_or(map, "file_manager", cfg.file_manager);

    // Indexing
    cfg.index_root = get_dir_or(map, "index_root", cfg.index_root);
    cfg.ignore_dirs = get_dirs_or(map, "ignore_dir", cfg.ignore_dirs);
    cfg.ignore_dir_names =
        get_strings_or(map, "ignore_dir_name", cfg.ignore_dir_names);

    std::vector<fs::path> commands_dirs{fs::path(KHALA_DATADIR) / "commands",
                                        path.parent_path() / "commands"};
    std::unordered_map<std::string, CustomActionDef> actions_by_stem;
    for (const auto &commands_dir : commands_dirs) {
        if (fs::exists(commands_dir)) {
            for (const auto &entry : fs::directory_iterator(commands_dir)) {
                if (entry.path().extension() != ".ini")
                    continue;

                auto map = parse_ini(entry.path());

                const bool is_file_action =
                    get_bool_or(map, "is_file_action", false);
                const bool stdout_to_clipboard =
                    get_bool_or(map, "stdout_to_clipboard", false);
                std::string title = get_string_or(map, "title", "");
                std::string description = get_string_or(map, "description", "");
                std::string shell_cmd = get_string_or(map, "shell_cmd", "");

                if (title.empty() || shell_cmd.empty())
                    continue;

                actions_by_stem.insert_or_assign(
                    entry.path().stem(),
                    CustomActionDef{
                        .title = title,
                        .description = description,
                        .shell_cmd = shell_cmd,
                        .is_file_action = is_file_action,
                        .stdout_to_clipboard = stdout_to_clipboard,
                    });
            }
        }
    }

    auto values = actions_by_stem | std::views::values;
    cfg.custom_actions.assign(values.begin(), values.end());
    return cfg;
}

void Config::save(const fs::path &path) const
{
    printf("Writing config to %s",
           fs::canonical(path).generic_string().c_str());
    std::ofstream file(path);

    file << "# Khala Launcher Configuration\n";
    file << "# This file is auto-generated with defaults on first run.\n";
    file << "\n";

    file << "# Window positioning and sizing (as percentages 0.0-1.0)\n";
    file << "width_ratio=" << width_ratio << "\n";
    file << "x_position=" << x_position << "\n";
    file << "y_position=" << y_position << "\n";
    file << "input_height_ratio=" << input_height_ratio << "\n";
    file << "item_height_ratio=" << item_height_ratio << "\n";
    file << "max_visible_items=" << max_visible_items << "\n";
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
    file << "\n";

    file << "# Indexing \n";
    file << "index_root=" << fs::canonical(index_root).generic_string() << "\n";
    for (const auto &dir : ignore_dirs) {
        file << "ignore_dir=" << fs::canonical(dir).generic_string() << "\n";
    }
    for (const auto &dir_name : ignore_dir_names) {
        file << "ignore_dir_name=" << dir_name << "\n";
    }
    file << "\n";
}