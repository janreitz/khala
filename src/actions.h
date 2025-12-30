#pragma once

#include <filesystem>
#include <string>
#include <variant>
#include <vector>

namespace fs = std::filesystem;

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

using Command = std::variant<OpenFile, OpenContainingFolder,
                             CopyPathToClipboard, CopyContentToClipboard,
                             CopyISOTimestamp, CopyUnixTimestamp, CopyUUID>;

struct Action {
    std::string title;
    std::string description;
    Command command;
};

std::vector<Action> make_file_actions(const fs::path &path);

const std::vector<Action>& get_utility_actions();

void process_command(const Command &command);