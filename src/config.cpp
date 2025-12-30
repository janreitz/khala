// config.cpp
#include "config.h"

#include <fstream>
#include <sstream>
#include <unordered_map>

namespace
{

std::unordered_map<std::string, std::string> parse_ini(const fs::path &path)
{
    std::unordered_map<std::string, std::string> result;
    std::ifstream file(path);
    std::string line;

    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#')
            continue;

        auto pos = line.find('=');
        if (pos != std::string::npos) {
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);
            result[key] = value;
        }
    }
    return result;
}

size_t get_size_or(const std::unordered_map<std::string, std::string> &map,
                   const std::string &key, size_t default_value)
{
    auto it = map.find(key);
    if (it == map.end())
        return default_value;
    try {
        return std::stoul(it->second);
    } catch (...) {
        return default_value;
    }
}

int get_int_or(const std::unordered_map<std::string, std::string> &map,
               const std::string &key, int default_value)
{
    auto it = map.find(key);
    if (it == map.end())
        return default_value;
    try {
        return std::stoi(it->second);
    } catch (...) {
        return default_value;
    }
}

bool get_bool_or(const std::unordered_map<std::string, std::string> &map,
                 const std::string &key, bool default_value)
{
    auto it = map.find(key);
    if (it == map.end())
        return default_value;
    return it->second == "true" || it->second == "1";
}

std::string
get_string_or(const std::unordered_map<std::string, std::string> &map,
              const std::string &key, std::string default_value)
{
    auto it = map.find(key);
    if (it == map.end())
        return default_value;
    return it->second;
}

double get_double_or(const std::unordered_map<std::string, std::string> &map,
                     const std::string &key, double default_value)
{
    auto it = map.find(key);
    if (it == map.end())
        return default_value;
    try {
        return std::stod(it->second);
    } catch (...) {
        return default_value;
    }
}

} // namespace

fs::path Config::default_path()
{
    const char *home = std::getenv("HOME");
    if (!home)
        return {};
    return fs::path(home) / ".khala" / "config.ini";
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
    cfg.input_height_ratio = get_double_or(map, "input_height_ratio", cfg.input_height_ratio);
    cfg.item_height_ratio = get_double_or(map, "item_height_ratio", cfg.item_height_ratio);
    cfg.max_visible_items =
        get_size_or(map, "max_visible_items", cfg.max_visible_items);

    // Appearance
    cfg.font_name = get_string_or(map, "font_name", cfg.font_name);
    cfg.font_size = get_int_or(map, "font_size", cfg.font_size);

    // Behavior
    cfg.quit_on_action = get_bool_or(map, "quit_on_action", cfg.quit_on_action);
    cfg.editor = get_string_or(map, "editor", cfg.editor);
    cfg.file_manager = get_string_or(map, "file_manager", cfg.file_manager);

    fs::path commands_dir = path.parent_path() / "commands";
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

            cfg.custom_actions.push_back(CustomActionDef{
                .title = title,
                .description = description,
                .shell_cmd = shell_cmd,
                .is_file_action = is_file_action,
                .stdout_to_clipboard = stdout_to_clipboard,
            });
        }
    }

    return cfg;
}

void Config::save(const fs::path &path) const
{
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
    file << "font_name=" << font_name << "\n";
    file << "font_size=" << font_size << "\n";
    file << "\n";

    file << "# Behavior\n";
    file << "quit_on_action=" << (quit_on_action ? "true" : "false") << "\n";
    file << "editor=" << editor << "\n";
    file << "file_manager=" << file_manager << "\n";
}