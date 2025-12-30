#pragma once

#include "utility.h"
#include "actions.h"

#include <filesystem>
#include <vector>

namespace indexer
{
PackedStrings scan_filesystem_parallel(const std::filesystem::path &root_path);

struct DesktopApp {
    std::string name;
    std::string description;
    std::string exec_command;
    std::string desktop_file_path;
};

std::vector<DesktopApp> scan_desktop_files();
} // namespace indexer
