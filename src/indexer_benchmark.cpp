#include "indexer.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

int main(int argc, char *argv[])
{
    // Get root path from args or use home directory
    const fs::path root_path =
        (argc > 1) ? argv[1] : fs::path(std::getenv("HOME"));

    // Database path - store in current directory for now
    const std::string db_path = "index.db";

    printf("Khala Indexer Benchmark\n");
    printf("=======================\n\n");

    try {
        auto total_start = std::chrono::steady_clock::now();

        printf("Starting two-phase filesystem indexing\n");
        printf("  Root: %s\n", root_path.string().c_str());
        printf("=================================\n");

        // Phase 1: Collect all paths in memory
        auto paths = indexer::scan_filesystem_parallel(root_path, 0);
        auto scan_end = std::chrono::steady_clock::now();
        auto scan_duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(scan_end -
                                                                  total_start);

        auto total_end = std::chrono::steady_clock::now();
        auto total_duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(total_end -
                                                                  total_start);

        printf("=================================\n");
        printf("Indexing complete! Scan time: %ldms Total time: %ldms\n",
               scan_duration.count(), total_duration.count());
    } catch (const std::exception &e) {
        fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }

    printf("\nIndexing finished!\n");
    return 0;
}
