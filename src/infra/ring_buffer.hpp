#pragma once

/**
 * SAGE Lock-Free SPSC Ring Buffer
 * Production-grade single-producer single-consumer queue
 * 
 * Optimizations:
 * - Cache-line isolated head/tail pointers
 * - Memory prefetching
 * - Power-of-2 capacity for bitmasking
 * - Acquire-release memory ordering
 * - No dynamic allocation
 * 
 * Target latency: <20ns push/pop
 */

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <type_traits>
#include <new>

#include "../core/compiler.hpp"
#include "../core/constants.hpp"

namespace sage {

/**
 * Lock-free SPSC (Single Producer Single Consumer) Ring Buffer
 * 
 * @tparam T     Element type (should be trivially copyable)
 * @tparam N     Capacity (must be power of 2)
 */
template<typename T, size_t N>
class RingBuffer {
    static_assert((N & (N - 1)) == 0, "Capacity must be power of 2");
    static_assert(N >= 16, "Capacity must be at least 16");
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
    
public:
    static constexpr size_t CAPACITY = N;
    static constexpr size_t MASK = N - 1;
    
    RingBuffer() noexcept {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }
    
    // ========================================================================
    // Producer Interface (Single Thread)
    // ========================================================================
    
    /**
     * Attempt to push an element (non-blocking)
     * @return true if successful, false if buffer is full
     */
    SAGE_HOT SAGE_FLATTEN
    bool try_push(const T& item) noexcept {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next_head = head + 1;
        
        // Check if buffer is full
        // We cache tail locally to avoid unnecessary atomic loads
        if (next_head - cached_tail_ > N) [[unlikely]] {
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if (next_head - cached_tail_ > N) {
                return false;  // Buffer full
            }
        }
        
        // Prefetch next write location
        SAGE_PREFETCH_WRITE(&buffer_[(next_head) & MASK]);
        
        // Write data
        buffer_[head & MASK] = item;
        
        // Publish to consumer
        head_.store(next_head, std::memory_order_release);
        
        return true;
    }
    
    /**
     * Push with spin-wait (blocks if full)
     * Use with caution - can cause latency spikes
     */
    SAGE_HOT
    void push_blocking(const T& item) noexcept {
        while (!try_push(item)) [[unlikely]] {
            SAGE_CPU_PAUSE();
        }
    }
    
    // ========================================================================
    // Consumer Interface (Single Thread)
    // ========================================================================
    
    /**
     * Attempt to pop an element (non-blocking)
     * @return true if successful, false if buffer is empty
     */
    SAGE_HOT SAGE_FLATTEN
    bool try_pop(T& item) noexcept {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        
        // Check if buffer is empty
        if (cached_head_ == tail) [[unlikely]] {
            cached_head_ = head_.load(std::memory_order_acquire);
            if (cached_head_ == tail) {
                return false;  // Buffer empty
            }
        }
        
        // Prefetch next read location
        SAGE_PREFETCH_READ(&buffer_[(tail + 1) & MASK]);
        
        // Read data
        item = buffer_[tail & MASK];
        
        // Advance tail
        tail_.store(tail + 1, std::memory_order_release);
        
        return true;
    }
    
    /**
     * Peek at front element without removing
     */
    SAGE_HOT
    bool try_peek(T& item) const noexcept {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t head = head_.load(std::memory_order_acquire);
        
        if (head == tail) {
            return false;
        }
        
        item = buffer_[tail & MASK];
        return true;
    }
    
    /**
     * Pop with spin-wait (blocks if empty)
     */
    SAGE_HOT
    void pop_blocking(T& item) noexcept {
        while (!try_pop(item)) [[unlikely]] {
            SAGE_CPU_PAUSE();
        }
    }
    
    // ========================================================================
    // Batch Operations
    // ========================================================================
    
    /**
     * Pop multiple elements at once
     * @return Number of elements actually popped
     */
    SAGE_HOT
    size_t try_pop_batch(T* items, size_t max_count) noexcept {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t head = head_.load(std::memory_order_acquire);
        
        const size_t available = head - tail;
        const size_t to_pop = (available < max_count) ? available : max_count;
        
        if (to_pop == 0) {
            return 0;
        }
        
        // Copy items
        for (size_t i = 0; i < to_pop; ++i) {
            items[i] = buffer_[(tail + i) & MASK];
        }
        
        // Advance tail
        tail_.store(tail + to_pop, std::memory_order_release);
        
        return to_pop;
    }
    
    // ========================================================================
    // Capacity Queries (thread-safe but approximate)
    // ========================================================================
    
    size_t size_approx() const noexcept {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t tail = tail_.load(std::memory_order_relaxed);
        return head - tail;
    }
    
    bool empty_approx() const noexcept {
        return size_approx() == 0;
    }
    
    bool full_approx() const noexcept {
        return size_approx() >= N;
    }
    
    static constexpr size_t capacity() noexcept {
        return N;
    }
    
private:
    // Producer state (one cache line)
    SAGE_CACHE_ALIGNED std::atomic<size_t> head_{0};
    size_t cached_tail_{0};  // Producer's cached tail
    char pad1_[CACHE_LINE_SIZE - sizeof(std::atomic<size_t>) - sizeof(size_t)];
    
    // Consumer state (one cache line)
    SAGE_CACHE_ALIGNED std::atomic<size_t> tail_{0};
    size_t cached_head_{0};  // Consumer's cached head
    char pad2_[CACHE_LINE_SIZE - sizeof(std::atomic<size_t>) - sizeof(size_t)];
    
    // Data buffer (separate cache lines from control)
    SAGE_CACHE_ALIGNED T buffer_[N];
};

// ============================================================================
// Type Aliases
// ============================================================================

// Default sizes for common use cases
template<typename T>
using SmallRingBuffer = RingBuffer<T, 1024>;

template<typename T>
using MediumRingBuffer = RingBuffer<T, 65536>;

template<typename T>
using LargeRingBuffer = RingBuffer<T, 1048576>;

} // namespace sage
