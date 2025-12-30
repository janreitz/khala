// config.h
#pragma once

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

struct Config {
    // Window
    int width = 800;
    int input_height = 40;
    int item_height = 24;
    size_t max_visible_items = 10;

    // Appearance
    std::string font_name = "monospace";
    int font_size = 14;

    // Behavior
    std::string editor = "xdg-open"; // Use default application
    std::string file_manager = "xdg-open";
    bool quit_on_action = true;

    // Paths
    static fs::path default_path();
    static Config load(const fs::path &path);
    void save(const fs::path &path) const;

    fs::path config_path;
};