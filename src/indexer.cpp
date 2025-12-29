#include "indexer.h"
#include "utility.h"

#include <cstdio>
#include <filesystem>
#include <future>
#include <iterator>
#include <vector>

namespace fs = std::filesystem;

namespace indexer {

PackedStrings scan_subtree(const fs::path& root) {
    PackedStrings paths;
    try {
        for (const auto& entry : fs::recursive_directory_iterator(
                 root, fs::directory_options::skip_permission_denied)) {
            if (entry.is_regular_file()) {
                paths.push(entry.path().string());
            }
        }
    } catch (const fs::filesystem_error&) {
    }
    paths.shrink_to_fit();
    return paths;
}

PackedStrings scan_filesystem_parallel(const fs::path& root_path) {
    PackedStrings result;
    std::vector<fs::path> subdirs;
    
    // Collect top-level entries
    try {
        for (const auto& entry : fs::directory_iterator(root_path)) {
            if (entry.is_directory()) {
                subdirs.push_back(entry.path());
            } else if (entry.is_regular_file()) {
                result.push(entry.path().string());
            }
        }
    } catch (const fs::filesystem_error& e) {
        fprintf(stderr, "Error reading root: %s\n", e.what());
        return result;
    }

    std::vector<std::future<PackedStrings>> futures;
    futures.reserve(subdirs.size());
    for (const auto& subdir : subdirs) {
        futures.push_back(std::async(std::launch::async, scan_subtree, subdir));
    }

    for (auto& fut : futures) {
        result.merge(fut.get());
    }

    return result;
}

} // namespace indexer