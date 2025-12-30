// config.h
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct CustomActionDef {
    std::string title;
    std::string description;
    std::string shell_cmd;
    bool is_file_action;  // true = file action, false = global
};

struct Config {
    // Window positioning and sizing (as percentages of screen size, 0.0-1.0)
    double width_ratio = 0.4;      // 40% of screen width
    double x_position = 0.5;       // center horizontally (0.5 = 50% from left)
    double y_position = 0.25;      // 25% from top
    
    // Window appearance (as percentages of screen height)
    double input_height_ratio = 0.025;   // 2.5% of screen height
    double item_height_ratio = 0.018;    // 1.8% of screen height
    size_t max_visible_items = 10;

    // Appearance
    std::string font_name = "monospace";
    int font_size = 14;

    // Behavior
    std::string editor = "xdg-open"; // Use default application
    std::string file_manager = "xdg-open";
    bool quit_on_action = true;

    // Custom Actions
    std::vector<CustomActionDef> custom_actions;

    // Paths
    static fs::path default_path();
    fs::path config_path;
    
    static Config load(const fs::path &path);
    void save(const fs::path &path) const;
};