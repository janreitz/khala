#include "streamingindex.h"
#include "packed_strings.h"

void StreamingIndex::add_chunk(PackedStrings &&chunk)
{
    if (chunk.empty())
        return;

    auto shared_chunk = std::make_shared<const PackedStrings>(std::move(chunk));
    {
        std::lock_guard lock(mutex_);
        total_files_ += shared_chunk->size();
        chunks_.push_back(std::move(shared_chunk));
    }
    chunk_available_.notify_one();
}

void StreamingIndex::mark_scan_complete()
{
    {
        std::lock_guard lock(mutex_);
        scan_complete_ = true;
    }
    chunk_available_.notify_all();
}

bool StreamingIndex::is_scan_complete() const
{
    std::lock_guard lock(mutex_);
    return scan_complete_;
}

size_t StreamingIndex::get_available_chunks() const
{
    std::lock_guard lock(mutex_);
    return chunks_.size();
}

size_t StreamingIndex::get_total_files() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return total_files_;
}

std::shared_ptr<const PackedStrings>
StreamingIndex::get_chunk(size_t index) const
{
    std::lock_guard lock(mutex_);
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

void StreamingIndex::clear()
{
    std::lock_guard lock(mutex_);
    chunks_.clear();
    total_files_ = 0;
    scan_complete_ = false;
}