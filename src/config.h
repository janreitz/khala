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

    // Custom Actions
    std::vector<CustomActionDef> custom_actions;

    // Paths
    static fs::path default_path();
    fs::path config_path;
    
    static Config load(const fs::path &path);
    void save(const fs::path &path) const;
};