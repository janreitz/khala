#include "utility.h"

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <sqlite3.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace asio = boost::asio;
namespace fs = std::filesystem;

// Phase 1: Async filesystem traversal with periodic yielding
asio::awaitable<std::vector<std::string>>
scan_filesystem_to_memory(const fs::path &root_path,
                          asio::thread_pool &thread_pool)
{
    auto start_time = std::chrono::steady_clock::now();
    std::cout << "Phase 1: Scanning filesystem from " << root_path << std::endl;

    // Offload blocking filesystem operations to thread pool while keeping
    // coroutine benefits
    auto paths = co_await asio::co_spawn(
        thread_pool,
        [root_path]() -> asio::awaitable<std::vector<std::string>> {
            std::vector<std::string> collected_paths;
            size_t processed_entries = 0;

            try {
                for (const auto &entry : fs::recursive_directory_iterator(
                         root_path,
                         fs::directory_options::skip_permission_denied)) {

                    if (entry.is_regular_file()) {
                        collected_paths.push_back(entry.path().string());
                    }

                    processed_entries++;

                    // Yield control every 1000 entries to keep responsive
                    if (processed_entries % 1000 == 0) {
                        co_await asio::post(asio::use_awaitable);

                        // Progress update every 10k entries
                        if (processed_entries % 10000 == 0) {
                            std::cout << "  Processed " << processed_entries
                                      << " entries, found "
                                      << collected_paths.size() << " files..."
                                      << std::endl;
                        }
                    }
                }
            } catch (const fs::filesystem_error &e) {
                std::cerr << "Filesystem error: " << e.what() << std::endl;
            } catch (const std::exception &e) {
                std::cerr << "Error during filesystem scan: " << e.what()
                          << std::endl;
            }

            co_return collected_paths;
        },
        asio::use_awaitable);

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    std::cout << "Phase 1 complete: Found " << paths.size() << " files in "
              << duration.count() << "ms" << std::endl;

    co_return paths;
}

// Phase 2: Batched SQLite writes with transactions
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

    // Enable WAL mode for better concurrency (though we're
    // single-threaded here)
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
asio::awaitable<void> index_filesystem(const fs::path &root_path,
                                       const std::string &db_path,
                                       asio::thread_pool &thread_pool)
{
    auto total_start = std::chrono::steady_clock::now();

    std::cout << "Starting two-phase filesystem indexing" << std::endl;
    std::cout << "  Root: " << root_path << std::endl;
    std::cout << "  Database: " << db_path << std::endl;
    std::cout << "=================================" << std::endl;

    // Phase 1: Collect all paths in memory
    auto paths = co_await scan_filesystem_to_memory(root_path, thread_pool);

    // Phase 2: Batch write to SQLite
    write_paths_batched(paths, db_path);

    auto total_end = std::chrono::steady_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        total_end - total_start);

    std::cout << "=================================" << std::endl;
    std::cout << "Indexing complete! Total time: " << total_duration.count()
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

    // Create io_context (event loop) and thread pool for blocking operations
    asio::io_context io;
    asio::thread_pool thread_pool(std::thread::hardware_concurrency());

    try {
        // Spawn the main indexing coroutine
        asio::co_spawn(io, index_filesystem(root_path, db_path, thread_pool),
                       asio::detached);

        // Run the event loop
        io.run();

    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "\nIndexing finished!\n";
    return 0;
}