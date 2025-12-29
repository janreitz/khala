#include "fuzzy.h"
#include "indexer.h"
#include "utility.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

int main(int argc, char *argv[])
{
    // Get root path from args or use home directory
    const fs::path root_path =
        (argc > 1) ? argv[1] : fs::path(std::getenv("HOME"));

    printf("=======================\n\n");
    printf("Khala Indexer Benchmark\n");

    try {
        auto total_start = std::chrono::steady_clock::now();

        printf("  Root: %s\n", root_path.string().c_str());
        printf("=================================\n");

        // Phase 1: Collect all paths in memory
        auto paths = indexer::scan_filesystem_parallel(root_path);
        auto scan_end = std::chrono::steady_clock::now();
        auto scan_duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(scan_end -
                                                                  total_start);

        auto ranked = rank_parallel(paths, [](std::string_view path) {
            return fuzzy::fuzzy_score(path, "query");
        }, 20);

        auto rank_end = std::chrono::steady_clock::now();
        auto rank_duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(rank_end -
                                                                  scan_end);
        auto total_duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(rank_end -
                                                                  total_start);

        printf("=================================\n");
        printf("Indexing complete! Scan time: %ldms Rank time %ldms Total time: %ldms\n",
               scan_duration.count(), rank_duration.count(), total_duration.count());
    } catch (const std::exception &e) {
        fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }

    printf("\nIndexing finished!\n");
    return 0;
}
