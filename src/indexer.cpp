#include "indexer.h"
#include "utility.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>
#include <future>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace indexer {


std::vector<std::string> scan_subtree(const fs::path& root) {
    std::vector<std::string> paths;
    try {
        for (const auto& entry : fs::recursive_directory_iterator(
                 root, fs::directory_options::skip_permission_denied)) {
            if (entry.is_regular_file()) {
                paths.push_back(entry.path().string());
            }
        }
    } catch (const fs::filesystem_error&) {
        // Handle or ignore
    }
    return paths;
}

std::vector<std::string> scan_filesystem_parallel(const fs::path& root_path) {
    std::vector<std::string> result;
    std::vector<fs::path> subdirs;
    
    // Collect top-level entries
    try {
        for (const auto& entry : fs::directory_iterator(root_path)) {
            if (entry.is_directory()) {
                subdirs.push_back(entry.path());
            } else if (entry.is_regular_file()) {
                result.push_back(entry.path().string());
            }
        }
    } catch (const fs::filesystem_error& e) {
        fprintf(stderr, "Error reading root: %s\n", e.what());
        return result;
    }


    std::vector<std::future<std::vector<std::string>>> futures;
    futures.reserve(subdirs.size());
    for (const auto& subdir : subdirs) {
        futures.push_back(std::async(std::launch::async, scan_subtree, subdir));
    }

    // Gather results
    for (auto& fut : futures) {
        auto paths = fut.get();
        result.insert(result.end(), 
                      std::make_move_iterator(paths.begin()),
                      std::make_move_iterator(paths.end()));
    }

    return result;
}

} // namespace indexer