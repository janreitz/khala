#pragma once

#include "config.h"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>

namespace ui
{
struct Item;
}

namespace fs = std::filesystem;

// Effects that commands/events can request
// These are processed after event handling completes
struct QuitApplication {
};
struct HideWindow {
};
struct ReloadIndexEffect {
};

using Effect = std::variant<QuitApplication, HideWindow, ReloadIndexEffect>;

struct Noop {
};

// File commands
struct OpenFileCommand {
    fs::path path;
};
struct OpenDirectory {
    fs::path path;
};
struct RemoveFile {
    fs::path path;
};
struct RemoveFileRecursive {
    fs::path path;
};
struct CopyPathToClipboard {
    fs::path path;
};
struct CopyContentToClipboard {
    fs::path path;
};

// Utility commands (not file-specific)
struct ReloadIndex {
};
struct CopyISOTimestamp {
};
struct CopyUnixTimestamp {
};
struct CopyUUID {
};

struct CustomCommand {
    std::optional<fs::path> path;
    std::string shell_cmd;
    std::string shell; // shell to use for execution
    bool stdout_to_clipboard = false;
};

using Command =
    std::variant<Noop, OpenFileCommand, OpenDirectory, RemoveFile,
                 RemoveFileRecursive, CopyPathToClipboard,
                 CopyContentToClipboard, ReloadIndex, CopyISOTimestamp,
                 CopyUnixTimestamp, CopyUUID, CustomCommand>;

/// @brief Return false to short-circuit iteration
typedef bool (*ActionCallback)(const void *file_action,
                                   void *user_data);
void for_each_file_action(const fs::path &path, const Config &config,
                          ActionCallback cb, void *user_data);

void for_each_global_action(const Config &config, ActionCallback cb, void *user_data);

std::expected<std::optional<Effect>, std::string>
process_command(const Command &command, const Config &config);