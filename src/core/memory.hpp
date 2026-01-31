#pragma once

/**
 * SAGE Memory Management
 * Huge pages, NUMA-aware allocation, locked memory
 */

#include <cstdint>
#include <cstddef>
#include <cstring>

#ifdef __linux__
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include "compiler.hpp"
#include "constants.hpp"

namespace sage {
namespace memory {

// ============================================================================
// Page-Aligned Allocation
// ============================================================================

/**
 * Allocate page-aligned memory
 */
inline void* alloc_aligned(size_t size, size_t alignment = PAGE_SIZE) noexcept {
    void* ptr = nullptr;
    if (posix_memalign(&ptr, alignment, size) != 0) {
        return nullptr;
    }
    return ptr;
}

/**
 * Free page-aligned memory
 */
inline void free_aligned(void* ptr) noexcept {
    free(ptr);
}

// ============================================================================
// Huge Pages (2MB)
// ============================================================================

/**
 * Allocate memory using 2MB huge pages
 * Reduces TLB misses for large buffers
 */
inline void* alloc_huge_pages(size_t size) noexcept {
#ifdef __linux__
    // Round up to huge page boundary
    size = (size + HUGE_PAGE_SIZE - 1) & ~(HUGE_PAGE_SIZE - 1);
    
    void* ptr = mmap(nullptr, size,
                     PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB,
                     -1, 0);
    
    if (ptr == MAP_FAILED) {
        // Fallback to regular pages
        ptr = mmap(nullptr, size,
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS,
                   -1, 0);
    }
    
    return (ptr == MAP_FAILED) ? nullptr : ptr;
#else
    return alloc_aligned(size, PAGE_SIZE);
#endif
}

/**
 * Free huge page allocation
 */
inline void free_huge_pages(void* ptr, size_t size) noexcept {
#ifdef __linux__
    size = (size + HUGE_PAGE_SIZE - 1) & ~(HUGE_PAGE_SIZE - 1);
    munmap(ptr, size);
#else
    free_aligned(ptr);
#endif
}

// ============================================================================
// Locked Memory (Non-Swappable)
// ============================================================================

/**
 * Lock memory pages to prevent swapping
 * Critical for deterministic latency
 */
inline bool lock_memory(void* ptr, size_t size) noexcept {
#ifdef __linux__
    return mlock(ptr, size) == 0;
#else
    (void)ptr;
    (void)size;
    return false;
#endif
}

/**
 * Unlock memory pages
 */
inline bool unlock_memory(void* ptr, size_t size) noexcept {
#ifdef __linux__
    return munlock(ptr, size) == 0;
#else
    (void)ptr;
    (void)size;
    return false;
#endif
}

/**
 * Lock all current and future memory allocations
 * Requires CAP_IPC_LOCK or root
 */
inline bool lock_all_memory() noexcept {
#ifdef __linux__
    return mlockall(MCL_CURRENT | MCL_FUTURE) == 0;
#else
    return false;
#endif
}

// ============================================================================
// Prefault Pages
// ============================================================================

/**
 * Touch all pages to fault them into memory
 * Call after allocation to avoid page faults in hot path
 */
inline void prefault_pages(void* ptr, size_t size) noexcept {
    volatile char* p = static_cast<volatile char*>(ptr);
    for (size_t i = 0; i < size; i += PAGE_SIZE) {
        p[i] = 0;
    }
}

// ============================================================================
// Cache Line Operations
// ============================================================================

/**
 * Flush cache line to memory
 */
SAGE_ALWAYS_INLINE void cache_flush(const void* ptr) noexcept {
#if defined(__x86_64__)
    asm volatile("clflush (%0)" :: "r"(ptr) : "memory");
#else
    (void)ptr;
#endif
}

/**
 * Write back and invalidate cache line
 */
SAGE_ALWAYS_INLINE void cache_writeback(const void* ptr) noexcept {
#if defined(__x86_64__)
    asm volatile("clwb (%0)" :: "r"(ptr) : "memory");
#else
    (void)ptr;
#endif
}

// ============================================================================
// Shared Memory
// ============================================================================

/**
 * Create or open shared memory segment
 * Returns file descriptor or -1 on error
 */
inline int shm_create(const char* name, size_t size, bool& created) noexcept {
#ifdef __linux__
    int fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd >= 0) {
        created = true;
        if (ftruncate(fd, size) != 0) {
            close(fd);
            shm_unlink(name);
            return -1;
        }
    } else {
        // Already exists, open it
        fd = shm_open(name, O_RDWR, 0600);
        created = false;
    }
    return fd;
#else
    (void)name;
    (void)size;
    (void)created;
    return -1;
#endif
}

/**
 * Map shared memory into address space
 */
inline void* shm_map(int fd, size_t size) noexcept {
#ifdef __linux__
    void* ptr = mmap(nullptr, size,
                     PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd, 0);
    return (ptr == MAP_FAILED) ? nullptr : ptr;
#else
    (void)fd;
    (void)size;
    return nullptr;
#endif
}

/**
 * Unmap shared memory
 */
inline void shm_unmap(void* ptr, size_t size) noexcept {
#ifdef __linux__
    munmap(ptr, size);
#else
    (void)ptr;
    (void)size;
#endif
}

/**
 * Remove shared memory segment
 */
inline void shm_remove(const char* name) noexcept {
#ifdef __linux__
    shm_unlink(name);
#else
    (void)name;
#endif
}

} // namespace memory
} // namespace sage
