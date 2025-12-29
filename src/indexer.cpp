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

std::vector<PackedStrings> scan_filesystem_parallel(const fs::path& root_path) {
    std::vector<PackedStrings> result;
    std::vector<fs::path> subdirs;
    
    // Collect top-level entries
    PackedStrings top_level_paths;
    try {
        for (const auto& entry : fs::directory_iterator(root_path)) {
            if (entry.is_directory()) {
                subdirs.push_back(entry.path());
            } else if (entry.is_regular_file()) {
                top_level_paths.push(entry.path().string());
            }
        }
    } catch (const fs::filesystem_error& e) {
        fprintf(stderr, "Error reading root: %s\n", e.what());
        return result;
    }
    result.push_back(top_level_paths);


    std::vector<std::future<PackedStrings>> futures;
    futures.reserve(subdirs.size());
    for (const auto& subdir : subdirs) {
        futures.push_back(std::async(std::launch::async, scan_subtree, subdir));
    }

    // Gather results
    for (auto& fut : futures) {
        auto paths = fut.get();
        result.push_back(std::move(paths));
    }

    return result;
}

} // namespace indexer