#pragma once

#include <atomic>
#include <utility>

/// Lock-free single-slot register with last-writer-wins semantics.
/// Optimized for single-producer, single-consumer scenarios where only
/// the most recent value matters and intermediate values can be discarded.
///
/// Thread-safe for concurrent writes and reads, but note that rapid writes
/// will cause intermediate values to be lost (which is the intended behavior).
template <typename T>
class LastWriterWinsSlot {
private:
    std::atomic<T*> latest{nullptr};

public:
    LastWriterWinsSlot() = default;

    ~LastWriterWinsSlot() {
        delete latest.load(std::memory_order_acquire);
    }

    // Non-copyable, non-movable
    LastWriterWinsSlot(const LastWriterWinsSlot&) = delete;
    LastWriterWinsSlot& operator=(const LastWriterWinsSlot&) = delete;

    /// Writes a new value, replacing any previous value.
    /// The previous value is discarded (last-writer-wins semantics).
    void write(T&& value) {
        T* new_ptr = new T(std::move(value));
        T* old_ptr = latest.exchange(new_ptr, std::memory_order_acq_rel);
        delete old_ptr;
    }

    /// Attempts to read the latest value.
    /// Returns true and moves the value into 'out' if a value was available.
    /// Returns false if no new value is available.
    /// After a successful read, the slot is cleared until the next write.
    bool try_read(T& out) {
        T* ptr = latest.exchange(nullptr, std::memory_order_acquire);
        if (!ptr) {
            return false;
        }
        out = std::move(*ptr);
        delete ptr;
        return true;
    }

    /// Checks if a value is available without consuming it.
    bool has_value() const {
        return latest.load(std::memory_order_acquire) != nullptr;
    }
};
