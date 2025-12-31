#pragma once

#include "utility.h"
#include "actions.h"

#include <filesystem>
#include <vector>
#include <set>

namespace fs = std::filesystem;

namespace indexer
{
PackedStrings scan_filesystem_parallel(const std::filesystem::path &root_path,
                                      const std::set<fs::path> &ignore_dirs = {});

struct DesktopApp {
    std::string name;
    std::string description;
    std::string exec_command;
    fs::path desktop_file_path;
};

std::vector<DesktopApp> scan_desktop_files();
} // namespace indexer
