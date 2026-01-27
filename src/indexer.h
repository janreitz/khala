#pragma once

#include "packed_strings.h"
#include "streamingindex.h"
#include "actions.h"

#include <filesystem>
#include <vector>
#include <set>
#include <string>

namespace fs = std::filesystem;

namespace indexer
{
PackedStrings scan_filesystem_parallel(const std::set<std::filesystem::path> &root_paths,
                                      const std::set<fs::path> &ignore_dirs = {},
                                      const std::set<std::string> &ignore_dir_names = {});

void scan_filesystem_streaming(const std::set<std::filesystem::path> &root_paths,
                               StreamingIndex &index,
                               const std::set<fs::path> &ignore_dirs = {},
                               const std::set<std::string> &ignore_dir_names = {},
                               size_t chunk_size = 1000);
} // namespace indexer
