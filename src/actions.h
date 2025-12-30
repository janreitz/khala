#pragma once

#include "config.h"

#include <filesystem>
#include <string>
#include <variant>
#include <optional>
#include <vector>

namespace fs = std::filesystem;

// File commands
struct OpenFile {
    fs::path path;
};
struct OpenContainingFolder {
    fs::path path;
};
struct CopyPathToClipboard {
    fs::path path;
};
struct CopyContentToClipboard {
    fs::path path;
};

// Utility commands (not file-specific)
struct CopyISOTimestamp {
};
struct CopyUnixTimestamp {
};
struct CopyUUID {
};

struct CustomCommand {
    std::optional<fs::path> path;
    std::string shell_cmd;
};

using Command = std::variant<OpenFile, OpenContainingFolder,
                             CopyPathToClipboard, CopyContentToClipboard,
                             CopyISOTimestamp, CopyUnixTimestamp, CopyUUID, CustomCommand>;

struct Action {
    std::string title;
    std::string description;
    Command command;
};

std::vector<Action> make_file_actions(const fs::path &path, const Config& config);

std::vector<Action> get_global_actions(const Config& config);

void process_command(const Command &command, const Config& config);