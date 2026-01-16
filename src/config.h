#pragma once

#include "types.h"

#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct CustomActionDef {
    std::string title;
    std::string description;
    std::string shell_cmd;
    bool is_file_action;              // true = file action, false = global
    bool stdout_to_clipboard = false; // true = capture stdout to clipboard
};

struct Color {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    double a = 1.0;

    // Convert to Pango's 16-bit format
    uint16_t pango_red() const;
    uint16_t pango_green() const;
    uint16_t pango_blue() const;
};

std::optional<Color> parse_color(const std::string &str);

struct Config {
    // Appearance
    // Window positioning and sizing (as percentages of screen size, 0.0-1.0)
    double width_ratio = 0.5; // 50% of screen width
    double height_ratio = 0.6; // 60% of screen height
    double x_position = 0.25;  // center horizontally (0.5 = 50% from left)
    double y_position = 0.20; // 25% from top

    // Styling
    std::string font_name = "monospace";
    int font_size = 10;
    std::string theme = "default-light";

    // Colors (loaded from theme)
    Color input_background_color = *parse_color("#EBEBEB");
    Color background_color = *parse_color("#FFF");
    Color border_color = *parse_color("#E0E0E0");
    Color text_color = *parse_color("#000");
    Color selection_color = *parse_color("#4D99FF");
    Color selection_text_color = *parse_color("#FFF");
    Color description_color = *parse_color("#808080");
    Color selection_description_color = *parse_color("#D9D9D9");

    // Behavior
    std::string editor = "xdg-open"; // Use default application
    std::string file_manager = "xdg-open";
    bool quit_on_action = true;

    // Background mode (Windows and X11 only - ignored on Wayland)
    // When enabled, app starts hidden, registers global hotkey, and stays running.
    // On Wayland, global hotkeys are not supported due to protocol security restrictions,
    // so background mode is automatically disabled.
    bool background_mode = true;
    ui::KeyboardEvent hotkey // Global hotkey to show/hide window
    {
        .key = ui::KeyCode::Space, .modifiers = ui::KeyModifier::Alt,
        .character = std::nullopt,
    };
    ui::KeyboardEvent quit_hotkey
    {
        .key = ui::KeyCode::Q, .modifiers = ui::KeyModifier::Ctrl,
        .character = std::nullopt,
    };

    // Indexing
    static std::set<fs::path> default_index_roots();
    std::set<fs::path> index_roots = default_index_roots();
#ifdef PLATFORM_WIN32
    std::set<fs::path> ignore_dirs{"C:\\Windows", 
                                   "C:\\$Recycle.Bin"};
#else
    std::set<fs::path> ignore_dirs{"/proc"};
#endif

    std::set<std::string> ignore_dir_names = {
        ".git", "node_modules", "env",     ".svn",
        ".hg",  "__pycache__",  ".vscode", ".idea"};

    // Custom Actions
    std::vector<CustomActionDef> custom_actions;

    // Paths
    static fs::path default_path();
    fs::path config_path;

    static Config load(const fs::path &path);
    void save(const fs::path &path) const;

};

void load_theme(const std::string &theme_name,
                const std::vector<fs::path> &theme_dirs, Config &config);