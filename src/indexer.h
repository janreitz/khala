#pragma once

#include "utility.h"

#include <filesystem>
#include <vector>

namespace indexer
{
std::vector<PackedStrings>
scan_filesystem_parallel(const std::filesystem::path &root_path);
} // namespace indexer
