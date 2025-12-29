#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace indexer
{
std::vector<std::string>
scan_filesystem_parallel(const std::filesystem::path &root_path,
                         unsigned int num_threads = 0);
} // namespace indexer
