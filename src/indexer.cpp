#include "utility.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>
#include <future>
#include <string>
#include <vector>

namespace fs = std::filesystem;


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

std::vector<std::string> scan_filesystem_parallel(const fs::path& root_path,
                                                   unsigned int num_threads = 0) {
    if (num_threads == 0) {
        num_threads = std::thread::hardware_concurrency();
    }

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

// Main two-phase indexing coroutine
void index_filesystem_threads(const fs::path &root_path,
                                       const std::string &db_path)
{
    auto total_start = std::chrono::steady_clock::now();

    printf("Starting two-phase filesystem indexing\n");
    printf("  Root: %s\n", root_path.string().c_str());
    printf("  Database: %s\n", db_path.c_str());
    printf("=================================\n");

    // Phase 1: Collect all paths in memory
    auto paths = scan_filesystem_parallel(root_path, 0);
    auto scan_end = std::chrono::steady_clock::now();
    auto scan_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        scan_end - total_start);

    auto total_end = std::chrono::steady_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        total_end - total_start);

    printf("=================================\n");
    printf("Indexing complete! Scan time: %ldms Total time: %ldms\n", scan_duration.count(), total_duration.count());
}

int main(int argc, char *argv[])
{
    // Get root path from args or use home directory
    const fs::path root_path =
        (argc > 1) ? argv[1] : fs::path(std::getenv("HOME"));

    // Database path - store in current directory for now
    const std::string db_path = "index.db";

    printf("Launcher Indexer\n");
    printf("================\n\n");

    try {
        index_filesystem_threads(root_path, db_path);
    } catch (const std::exception &e) {
        fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }

    printf("\nIndexing finished!\n");
    return 0;
}