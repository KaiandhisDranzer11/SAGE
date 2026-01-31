#pragma once

/**
 * SAGE CPU Affinity & NUMA Utilities
 * Production-grade core pinning for deterministic latency
 */

#include <cstdint>
#include <sched.h>
#include <pthread.h>

#ifdef __linux__
#include <unistd.h>
#endif

#include "compiler.hpp"

namespace sage {
namespace cpu {

// ============================================================================
// CPU Affinity
// ============================================================================

/**
 * Pin current thread to a specific CPU core
 * Returns 0 on success, -1 on failure
 */
inline int pin_to_core(int core_id) noexcept {
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#else
    (void)core_id;
    return -1;  // Not supported
#endif
}

/**
 * Pin current thread to a set of CPU cores
 * Returns 0 on success, -1 on failure
 */
inline int pin_to_cores(const int* cores, size_t count) noexcept {
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (size_t i = 0; i < count; ++i) {
        CPU_SET(cores[i], &cpuset);
    }
    return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#else
    (void)cores;
    (void)count;
    return -1;
#endif
}

/**
 * Get current CPU core
 */
SAGE_ALWAYS_INLINE int get_current_core() noexcept {
#ifdef __linux__
    return sched_getcpu();
#else
    return -1;
#endif
}

/**
 * Get number of online CPUs
 */
inline int get_cpu_count() noexcept {
#ifdef __linux__
    return static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
#else
    return 1;
#endif
}

// ============================================================================
// Thread Priority
// ============================================================================

/**
 * Set real-time scheduling priority (requires root or CAP_SYS_NICE)
 * Priority range: 1-99 for SCHED_FIFO
 */
inline int set_realtime_priority(int priority) noexcept {
#ifdef __linux__
    struct sched_param param;
    param.sched_priority = priority;
    return pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
#else
    (void)priority;
    return -1;
#endif
}

/**
 * Set thread to low-priority (for background tasks)
 */
inline int set_idle_priority() noexcept {
#ifdef __linux__
    struct sched_param param;
    param.sched_priority = 0;
    return pthread_setschedparam(pthread_self(), SCHED_IDLE, &param);
#else
    return -1;
#endif
}

// ============================================================================
// CPU Pause / Yield
// ============================================================================

/**
 * CPU pause instruction - reduces power and improves spin-wait performance
 * ~10 cycles on modern Intel CPUs
 */
SAGE_ALWAYS_INLINE void pause() noexcept {
#if defined(__x86_64__) || defined(_M_X64)
    asm volatile("pause" ::: "memory");
#elif defined(__aarch64__)
    asm volatile("yield" ::: "memory");
#else
    // Fallback: compiler barrier
    SAGE_COMPILER_BARRIER();
#endif
}

/**
 * Spin wait with exponential backoff
 * Returns after condition becomes true or max_spins reached
 */
template<typename Predicate>
SAGE_HOT bool spin_wait(Predicate&& pred, uint32_t max_spins = 1000000) noexcept {
    uint32_t spins = 0;
    while (!pred()) {
        if (++spins > max_spins) {
            return false;  // Timeout
        }
        
        // Exponential backoff
        if (spins < 16) {
            pause();
        } else if (spins < 128) {
            for (int i = 0; i < 8; ++i) pause();
        } else {
            sched_yield();
        }
    }
    return true;
}

} // namespace cpu
} // namespace sage
