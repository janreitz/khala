#pragma once

#include "utility.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>

enum class RankerMode {
    Inactive,   // Desktop apps mode - ranker sleeps
    FileSearch, // Active file searching
    Paused      // Query changing - pause work
};

struct FileResult {
    std::string path;
    float score;

    bool operator>(const FileResult &other) const
    {
        return score > other.score;
    }
};

struct ResultUpdate {
    std::vector<FileResult> results;
    bool scan_complete = false;
    size_t total_files = 0;
    size_t processed_chunks = 0;

    ResultUpdate() = default;
    ResultUpdate(std::vector<FileResult> &&results_)
        : results(std::move(results_))
    {
    }
};

class StreamingIndex
{
  private:
    std::deque<std::shared_ptr<const PackedStrings>> chunks_;
    mutable std::mutex mutex_;
    mutable std::condition_variable chunk_available_;
    size_t total_files_{0};
    bool scan_complete_{false};

  public:
    StreamingIndex() = default;

    StreamingIndex(const StreamingIndex &) = delete;
    StreamingIndex &operator=(const StreamingIndex &) = delete;
    StreamingIndex(StreamingIndex &&) = delete;
    StreamingIndex &operator=(StreamingIndex &&) = delete;

    void add_chunk(PackedStrings &&chunk);
    void mark_scan_complete();
    [[nodiscard]] bool is_scan_complete() const;
    [[nodiscard]] size_t get_available_chunks() const;
    [[nodiscard]] size_t get_total_files() const;
    [[nodiscard]] std::shared_ptr<const PackedStrings>
    get_chunk(size_t chunk_index) const;
    void wait_for_chunks(size_t min_chunks) const;
    void clear();
};