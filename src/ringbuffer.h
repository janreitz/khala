#pragma once

#include <atomic>
#include <new>
#include <vector>

template <typename T, size_t N>
class RingBuffer {
    static_assert((N & (N - 1)) == 0, "Size must be power of 2");
    static_assert(N >= 2, "Size must be at least 2");

private:
    // alignas ensures that atomics are on separate cache lines
    alignas(64) std::atomic<size_t> write_pos_{0};
    alignas(64) std::atomic<size_t> read_pos_{0};
    
    std::vector<T> buffer_{N};
    static constexpr size_t mask_ = N - 1;

public:
    RingBuffer() = default;
    
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    RingBuffer(RingBuffer&&) = delete;
    RingBuffer& operator=(RingBuffer&&) = delete;

    template <typename U>
    bool try_push(U&& item) noexcept(std::is_nothrow_assignable_v<T&, U&&>) {
        const auto write = write_pos_.load(std::memory_order_relaxed);
        const auto next_write = (write + 1) & mask_;
        
        if (next_write == read_pos_.load(std::memory_order_acquire)) {
            return false;
        }
        
        buffer_[write] = std::forward<U>(item);
        write_pos_.store(next_write, std::memory_order_release);
        return true;
    }

    bool try_pop(T& item) noexcept(std::is_nothrow_move_assignable_v<T>) {
        const auto read = read_pos_.load(std::memory_order_relaxed);
        
        if (read == write_pos_.load(std::memory_order_acquire)) {
            return false;
        }
        
        item = std::move(buffer_[read]);
        read_pos_.store((read + 1) & mask_, std::memory_order_release);
        return true;
    }

    // Returns approximate size - immediately stale, use with caution
    [[nodiscard]] size_t size_approx() const noexcept {
        const auto write = write_pos_.load(std::memory_order_relaxed);
        const auto read = read_pos_.load(std::memory_order_relaxed);
        return (write - read) & mask_;
    }

    [[nodiscard]] static constexpr size_t capacity() noexcept {
        return N - 1;
    }
};