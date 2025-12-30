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

using Command = std::variant<OpenFile, OpenContainingFolder,
                             CopyPathToClipboard, CopyContentToClipboard>;

struct Action {
    std::string title;
    std::string description;
    std::vector<Command> commands;
};

std::vector<Action> make_file_actions(const fs::path &path);

void process_command(const Command& command);