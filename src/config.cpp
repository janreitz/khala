// config.cpp
#include "config.h"

#include <fstream>
#include <sstream>
#include <unordered_map>

namespace {

std::unordered_map<std::string, std::string> parse_ini(const fs::path& path) {
    std::unordered_map<std::string, std::string> result;
    std::ifstream file(path);
    std::string line;
    
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        
        auto pos = line.find('=');
        if (pos != std::string::npos) {
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);
            result[key] = value;
        }
    }
    return result;
}

size_t get_size_or(const std::unordered_map<std::string, std::string>& map,
           const std::string& key, size_t default_value) {
    auto it = map.find(key);
    if (it == map.end()) return default_value;
    try {
        return std::stoul(it->second);
    } catch (...) {
        return default_value;
    }
}

int get_int_or(const std::unordered_map<std::string, std::string>& map,
           const std::string& key, int default_value) {
    auto it = map.find(key);
    if (it == map.end()) return default_value;
    try {
        return std::stoi(it->second);
    } catch (...) {
        return default_value;
    }
}

bool get_bool_or(const std::unordered_map<std::string, std::string>& map,
            const std::string& key, bool default_value) {
    auto it = map.find(key);
    if (it == map.end()) return default_value;
    return it->second == "true" || it->second == "1";
}

std::string get_string_or(const std::unordered_map<std::string, std::string>& map,
                   const std::string& key, std::string default_value) {
    auto it = map.find(key);
    if (it == map.end()) return default_value;
    return it->second;
}

} // namespace

fs::path Config::default_path() {
    const char* home = std::getenv("HOME");
    if (!home) return {};
    return fs::path(home) / ".khala" / "config.ini";
}

Config Config::load(const fs::path& path) {
    Config cfg;
    
    if (!fs::exists(path)) {
        fs::create_directories(path.parent_path());
        cfg.save(path);
        return cfg;
    }
    
    auto map = parse_ini(path);
    
    // Window
    cfg.width = get_int_or(map, "width", cfg.width);
    cfg.input_height = get_int_or(map, "input_height", cfg.input_height);
    cfg.item_height = get_int_or(map, "item_height", cfg.item_height);
    cfg.max_visible_items = get_size_or(map, "max_visible_items", cfg.max_visible_items);
    
    // Appearance
    cfg.font_name = get_string_or(map, "font_name", cfg.font_name);
    cfg.font_size = get_int_or(map, "font_size", cfg.font_size);
    
    // Behavior
    cfg.quit_on_action = get_bool_or(map, "quit_on_action", cfg.quit_on_action);
    
    return cfg;
}

void Config::save(const fs::path& path) const {
    std::ofstream file(path);
    
    file << "# Khala Launcher Configuration\n";
    file << "# This file is auto-generated with defaults on first run.\n";
    file << "\n";
    
    file << "# Window dimensions\n";
    file << "width=" << width << "\n";
    file << "input_height=" << input_height << "\n";
    file << "item_height=" << item_height << "\n";
    file << "max_visible_items=" << max_visible_items << "\n";
    file << "\n";
    
    file << "# Appearance\n";
    file << "font_name=" << font_name << "\n";
    file << "font_size=" << font_size << "\n";
    file << "\n";
    
    file << "# Behavior\n";
    file << "quit_on_action=" << (quit_on_action ? "true" : "false") << "\n";
}