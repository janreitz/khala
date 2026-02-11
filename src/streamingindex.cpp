#include "streamingindex.h"
#include "packed_strings.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <memory>
#include <mutex>
#include <utility>

void StreamingIndex::add_chunk(PackedStrings &&chunk)
{
    if (chunk.empty())
        return;

    auto shared_chunk = std::make_shared<const PackedStrings>(std::move(chunk));
    {
        const std::lock_guard lock(mutex_);
        chunk_offsets_.push_back(total_files_);
        total_files_ += shared_chunk->size();
        chunks_.push_back(std::move(shared_chunk));
    }
    chunk_available_.notify_one();
}

void StreamingIndex::mark_scan_complete()
{
    {
        const std::lock_guard lock(mutex_);
        scan_complete_ = true;
    }
    chunk_available_.notify_all();
}

bool StreamingIndex::is_scan_complete() const
{
    const std::lock_guard lock(mutex_);
    return scan_complete_;
}

size_t StreamingIndex::get_available_chunks() const
{
    const std::lock_guard lock(mutex_);
    return chunks_.size();
}

size_t StreamingIndex::get_total_files() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return total_files_;
}

std::shared_ptr<const PackedStrings>
StreamingIndex::get_chunk(size_t index) const
{
    const std::lock_guard lock(mutex_);
    if (index >= chunks_.size())
        return nullptr;
    return chunks_[index];
}

void StreamingIndex::wait_for_chunks(size_t min_chunks) const
{
    std::unique_lock lock(mutex_);
    chunk_available_.wait(lock, [this, min_chunks] {
        return chunks_.size() >= min_chunks || scan_complete_;
    });
}

StrView StreamingIndex::at(size_t global_index) const
{
    const std::lock_guard lock(mutex_);
    assert(!chunks_.empty());
    assert(global_index < total_files_);

    // Binary search: find the last chunk whose offset <= global_index
    auto it = std::upper_bound(chunk_offsets_.begin(), chunk_offsets_.end(),
                               global_index);
    assert(it != chunk_offsets_.begin());
    --it;
    const size_t chunk_idx = static_cast<size_t>(it - chunk_offsets_.begin());
    const size_t local_index = global_index - *it;
    return chunks_[chunk_idx]->at(local_index);
}

void StreamingIndex::clear()
{
    const std::lock_guard lock(mutex_);
    chunks_.clear();
    chunk_offsets_.clear();
    total_files_ = 0;
    scan_complete_ = false;
}