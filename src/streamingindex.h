#pragma once

#include "str.h"
#include "packed_strings.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <vector>

class StreamingIndex
{
  private:
    std::deque<std::shared_ptr<const PackedStrings>> chunks_;
    std::vector<size_t> chunk_offsets_; // prefix-sum: chunk_offsets_[i] = total strings before chunk i
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
    [[nodiscard]] StrView at(size_t global_index) const;
    void clear();
};