#pragma once

#include "config.h"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace ui
{
struct Item;
}

namespace fs = std::filesystem;

// Effects that commands/events can request
// These are processed after event handling completes
struct QuitApplication {};
struct HideWindow {};
struct ReloadIndexEffect {};

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
    bool stdout_to_clipboard = false;
};

using Command = std::variant<Noop, OpenFileCommand, OpenDirectory, RemoveFile,
                             RemoveFileRecursive, CopyPathToClipboard,
                             CopyContentToClipboard, ReloadIndex, CopyISOTimestamp,
                             CopyUnixTimestamp, CopyUUID, CustomCommand>;

std::vector<ui::Item> make_file_actions(const fs::path &path,
                                        const Config &config);

std::vector<ui::Item> get_global_actions(const Config &config);

std::expected<std::optional<Effect>, std::string> process_command(const Command &command,
                                                                  const Config &config);