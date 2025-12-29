#include "utility.h"

#include <sqlite3.h>

#include <chrono>
#include <filesystem>
#include <thread>
#include <future>
#include <iostream>
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
        std::cerr << "Error reading root: " << e.what() << std::endl;
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

void write_paths_batched(const std::vector<std::string> &paths,
                         const std::string &db_path)
{
    if (paths.empty()) {
        std::cout << "No paths to write to database" << std::endl;
        return;
    }

    auto start_time = std::chrono::steady_clock::now();
    std::cout << "Phase 2: Writing " << paths.size() << " paths to database"
              << std::endl;

    sqlite3 *db = nullptr;

    // Open database
    int rc = sqlite3_open(db_path.c_str(), &db);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db)
                  << std::endl;
        return;
    }

    const defer close_db([db]() noexcept {
        if (db)
            sqlite3_close(db);
    });

    // Create table if it doesn't exist
    const char *create_table_sql =
        "CREATE TABLE IF NOT EXISTS files ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "path TEXT UNIQUE NOT NULL,"
        "indexed_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
        ")";

    rc = sqlite3_exec(db, create_table_sql, nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot create table: " << sqlite3_errmsg(db) << std::endl;
        return;
    }

    // Prepare statement for batch inserts
    const char *insert_sql = "INSERT OR REPLACE INTO files (path) VALUES (?)";
    sqlite3_stmt *stmt = nullptr;
    rc = sqlite3_prepare_v2(db, insert_sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot prepare statement: " << sqlite3_errmsg(db)
                  << std::endl;
        return;
    }

    const defer finalize_stmt([stmt]() noexcept {
        if (stmt)
            sqlite3_finalize(stmt);
    });

    sqlite3_exec(db, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA synchronous=NORMAL", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA cache_size=10000", nullptr, nullptr, nullptr);

    // Process in batches with transactions
    const size_t batch_size = 5000;
    size_t total_processed = 0;

    for (size_t i = 0; i < paths.size(); i += batch_size) {
        // Begin transaction
        sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);

        const size_t batch_end = std::min(i + batch_size, paths.size());

        for (size_t j = i; j < batch_end; ++j) {
            sqlite3_bind_text(stmt, 1, paths[j].c_str(), -1, SQLITE_STATIC);

            rc = sqlite3_step(stmt);
            if (rc != SQLITE_DONE) {
                std::cerr << "Insert failed: " << sqlite3_errmsg(db)
                          << std::endl;
            }

            sqlite3_reset(stmt);
        }

        // Commit transaction
        sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);

        total_processed += (batch_end - i);

        std::cout << "  Written " << total_processed << " / " << paths.size()
                  << " files..." << std::endl;
    }

    std::cout << "Database write complete: " << total_processed
              << " files stored" << std::endl;

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    std::cout << "Phase 2 complete: Database updated in " << duration.count()
              << "ms" << std::endl;
}

// Main two-phase indexing coroutine
void index_filesystem_threads(const fs::path &root_path,
                                       const std::string &db_path)
{
    auto total_start = std::chrono::steady_clock::now();

    std::cout << "Starting two-phase filesystem indexing" << std::endl;
    std::cout << "  Root: " << root_path << std::endl;
    std::cout << "  Database: " << db_path << std::endl;
    std::cout << "=================================" << std::endl;

    // Phase 1: Collect all paths in memory
    auto paths = scan_filesystem_parallel(root_path, 0);
    auto scan_end = std::chrono::steady_clock::now();
    auto scan_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        scan_end - total_start);

    // Phase 2: Batch write to SQLite
    write_paths_batched(paths, db_path);

    auto total_end = std::chrono::steady_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        total_end - total_start);

    std::cout << "=================================" << std::endl;
    std::cout << "Indexing complete! Scan time:" <<   scan_duration.count() << "ms Total time: " << total_duration.count()
              << "ms" << std::endl;
}

int main(int argc, char *argv[])
{
    // Get root path from args or use home directory
    const fs::path root_path =
        (argc > 1) ? argv[1] : fs::path(std::getenv("HOME"));

    // Database path - store in current directory for now
    const std::string db_path = "index.db";

    std::cout << "Launcher Indexer\n";
    std::cout << "================\n\n";

    try {
        index_filesystem_threads(root_path, db_path);
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "\nIndexing finished!\n";
    return 0;
}