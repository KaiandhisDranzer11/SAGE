#pragma once

/**
 * SAGE High-Resolution Timing
 * Nanosecond-precision timing using TSC (Time Stamp Counter)
 */

#include <cstdint>
#include <chrono>
#include <thread>
#include "compiler.hpp"
#include "constants.hpp"

namespace sage {
namespace timing {

// ============================================================================
// TSC (Time Stamp Counter) - Lowest latency timing
// ============================================================================

/**
 * Read TSC without serialization (~10 cycles)
 * Use for relative timing within same thread
 */
SAGE_ALWAYS_INLINE uint64_t rdtsc() noexcept {
    uint32_t lo, hi;
    asm volatile ("rdtsc" : "=a"(lo), "=d"(hi) :: "memory");
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

/**
 * Read TSC with serialization (~25 cycles)
 * Use for cross-thread timing or when precise ordering matters
 */
SAGE_ALWAYS_INLINE uint64_t rdtscp() noexcept {
    uint32_t lo, hi, aux;
    asm volatile ("rdtscp" : "=a"(lo), "=d"(hi), "=c"(aux) :: "memory");
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

/**
 * Read TSC with full serialization (CPUID + RDTSC)
 * Most accurate but ~50 cycles overhead
 */
SAGE_ALWAYS_INLINE uint64_t rdtsc_serialized() noexcept {
    uint32_t lo, hi;
    asm volatile (
        "cpuid\n\t"
        "rdtsc"
        : "=a"(lo), "=d"(hi)
        : "a"(0)
        : "rbx", "rcx", "memory"
    );
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

// ============================================================================
// TSC Calibration
// ============================================================================

/**
 * TSC frequency calibrator
 * Converts TSC ticks to nanoseconds
 */
class TSCCalibrator {
public:
    TSCCalibrator() noexcept {
        calibrate();
    }
    
    void calibrate() noexcept {
        constexpr int CALIBRATION_MS = 100;
        
        uint64_t start_tsc = rdtscp();
        auto start_time = std::chrono::steady_clock::now();
        
        std::this_thread::sleep_for(std::chrono::milliseconds(CALIBRATION_MS));
        
        uint64_t end_tsc = rdtscp();
        auto end_time = std::chrono::steady_clock::now();
        
        auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            end_time - start_time
        ).count();
        
        uint64_t elapsed_tsc = end_tsc - start_tsc;
        
        // Calculate ticks per nanosecond (fixed-point with 16 fractional bits)
        ticks_per_ns_fp16_ = (elapsed_tsc << 16) / elapsed_ns;
        
        // Also store as double for convenience
        ticks_per_ns_ = static_cast<double>(elapsed_tsc) / elapsed_ns;
    }
    
    SAGE_ALWAYS_INLINE uint64_t tsc_to_ns(uint64_t tsc) const noexcept {
        return (tsc << 16) / ticks_per_ns_fp16_;
    }
    
    SAGE_ALWAYS_INLINE uint64_t ns_to_tsc(uint64_t ns) const noexcept {
        return (ns * ticks_per_ns_fp16_) >> 16;
    }
    
    double get_ticks_per_ns() const noexcept { return ticks_per_ns_; }

private:
    uint64_t ticks_per_ns_fp16_{0};  // Fixed-point (16.16)
    double ticks_per_ns_{0.0};
};

// ============================================================================
// POSIX Clock Functions
// ============================================================================

/**
 * Get monotonic time in nanoseconds
 * More portable but higher latency (~20ns)
 */
SAGE_ALWAYS_INLINE uint64_t get_monotonic_ns() noexcept {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * NANOS_PER_SEC + ts.tv_nsec;
}

/**
 * Get realtime (wall clock) in nanoseconds
 * Use for timestamps in logs/audit
 */
SAGE_ALWAYS_INLINE uint64_t get_realtime_ns() noexcept {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * NANOS_PER_SEC + ts.tv_nsec;
}

// ============================================================================
// Latency Measurement Helper
// ============================================================================

/**
 * RAII-style latency timer
 * Usage:
 *   uint64_t latency_tsc;
 *   {
 *       LatencyTimer timer(latency_tsc);
 *       // ... code to measure ...
 *   }
 *   // latency_tsc now contains elapsed TSC ticks
 */
class LatencyTimer {
public:
    explicit LatencyTimer(uint64_t& result) noexcept
        : result_(result), start_(rdtsc()) {}
    
    ~LatencyTimer() noexcept {
        result_ = rdtsc() - start_;
    }
    
    // Non-copyable, non-movable
    LatencyTimer(const LatencyTimer&) = delete;
    LatencyTimer& operator=(const LatencyTimer&) = delete;

private:
    uint64_t& result_;
    uint64_t start_;
};

} // namespace timing
} // namespace sage
