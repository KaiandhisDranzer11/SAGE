#pragma once

/**
 * SAGE Latency Tracker
 * End-to-end latency measurement with tail distribution metrics
 * 
 * Tracks latency from exchange timestamp through decision generation.
 * Provides percentile statistics (p50, p99, p99.9, p99.99) for monitoring.
 * 
 * Uses reservoir sampling for memory-efficient percentile estimation.
 */

#include <cstdint>
#include <atomic>
#include <algorithm>
#include <cstring>
#include "../core/constants.hpp"
#include "../core/compiler.hpp"
#include "../core/timing.hpp"

namespace sage {
namespace ade {

/**
 * Latency histogram with fixed buckets
 * 
 * Fast O(1) update, O(1) percentile query.
 * Buckets: 0-100ns, 100-200ns, ..., 10μs+
 */
class LatencyHistogram {
public:
    static constexpr size_t BUCKET_SIZE_NS = 100;     // 100ns per bucket
    static constexpr size_t NUM_BUCKETS = 128;        // Up to 12.8μs
    static constexpr size_t OVERFLOW_BUCKET = NUM_BUCKETS - 1;
    
    LatencyHistogram() noexcept {
        reset();
    }
    
    /**
     * Record a latency sample
     */
    SAGE_HOT
    void record(uint64_t latency_ns) noexcept {
        size_t bucket = latency_ns / BUCKET_SIZE_NS;
        if (bucket >= NUM_BUCKETS) bucket = OVERFLOW_BUCKET;
        
        buckets_[bucket]++;
        total_count_++;
        total_latency_ += latency_ns;
        
        if (latency_ns < min_latency_) min_latency_ = latency_ns;
        if (latency_ns > max_latency_) max_latency_ = latency_ns;
    }
    
    /**
     * Get percentile value (0-100)
     */
    uint64_t percentile(double pct) const noexcept {
        if (total_count_ == 0) return 0;
        
        uint64_t target = static_cast<uint64_t>(total_count_ * pct / 100.0);
        uint64_t cumulative = 0;
        
        for (size_t i = 0; i < NUM_BUCKETS; ++i) {
            cumulative += buckets_[i];
            if (cumulative >= target) {
                return (i + 1) * BUCKET_SIZE_NS;
            }
        }
        
        return max_latency_;
    }
    
    // Common percentiles
    uint64_t p50() const noexcept { return percentile(50.0); }
    uint64_t p90() const noexcept { return percentile(90.0); }
    uint64_t p99() const noexcept { return percentile(99.0); }
    uint64_t p999() const noexcept { return percentile(99.9); }
    uint64_t p9999() const noexcept { return percentile(99.99); }
    
    /**
     * Get mean latency
     */
    uint64_t mean() const noexcept {
        if (total_count_ == 0) return 0;
        return total_latency_ / total_count_;
    }
    
    uint64_t min() const noexcept { return min_latency_; }
    uint64_t max() const noexcept { return max_latency_; }
    uint64_t count() const noexcept { return total_count_; }
    
    /**
     * Reset all statistics
     */
    void reset() noexcept {
        std::memset(buckets_, 0, sizeof(buckets_));
        total_count_ = 0;
        total_latency_ = 0;
        min_latency_ = UINT64_MAX;
        max_latency_ = 0;
    }

private:
    uint64_t buckets_[NUM_BUCKETS];
    uint64_t total_count_;
    uint64_t total_latency_;
    uint64_t min_latency_;
    uint64_t max_latency_;
};

/**
 * End-to-end latency tracker
 * 
 * Tracks latency across pipeline stages:
 * - Exchange → CAL (network + parsing)
 * - CAL → ADE (queue wait)
 * - ADE processing (analytics)
 * - Total end-to-end
 */
class LatencyTracker {
public:
    LatencyTracker() noexcept 
        : tsc_calibrator_() {}
    
    /**
     * Record end-to-end latency from exchange timestamp
     * 
     * @param exchange_ts Exchange timestamp (nanoseconds)
     * @param decision_ts Decision timestamp (nanoseconds)
     */
    SAGE_HOT
    void record_e2e(uint64_t exchange_ts, uint64_t decision_ts) noexcept {
        if (decision_ts > exchange_ts) {
            uint64_t latency = decision_ts - exchange_ts;
            e2e_histogram_.record(latency);
        }
    }
    
    /**
     * Record processing latency (ADE internal)
     * 
     * @param start_tsc TSC at processing start
     * @param end_tsc TSC at processing end
     */
    SAGE_HOT
    void record_processing(uint64_t start_tsc, uint64_t end_tsc) noexcept {
        uint64_t latency_ns = tsc_calibrator_.tsc_to_ns(end_tsc - start_tsc);
        processing_histogram_.record(latency_ns);
    }
    
    /**
     * Record queue wait time
     */
    SAGE_HOT
    void record_queue_wait(uint64_t enqueue_ts, uint64_t dequeue_ts) noexcept {
        if (dequeue_ts > enqueue_ts) {
            queue_histogram_.record(dequeue_ts - enqueue_ts);
        }
    }
    
    // Accessors for histograms
    const LatencyHistogram& e2e() const noexcept { return e2e_histogram_; }
    const LatencyHistogram& processing() const noexcept { return processing_histogram_; }
    const LatencyHistogram& queue() const noexcept { return queue_histogram_; }
    
    /**
     * Get summary statistics
     */
    struct Summary {
        uint64_t e2e_p50;
        uint64_t e2e_p99;
        uint64_t e2e_p999;
        uint64_t processing_mean;
        uint64_t queue_mean;
        uint64_t total_samples;
    };
    
    Summary summary() const noexcept {
        return {
            e2e_histogram_.p50(),
            e2e_histogram_.p99(),
            e2e_histogram_.p999(),
            processing_histogram_.mean(),
            queue_histogram_.mean(),
            e2e_histogram_.count()
        };
    }
    
    /**
     * Reset all histograms
     */
    void reset() noexcept {
        e2e_histogram_.reset();
        processing_histogram_.reset();
        queue_histogram_.reset();
    }

private:
    timing::TSCCalibrator tsc_calibrator_;
    LatencyHistogram e2e_histogram_;        // Exchange → decision
    LatencyHistogram processing_histogram_; // ADE internal processing
    LatencyHistogram queue_histogram_;      // Queue wait time
};

/**
 * Stage latency breakdown
 * 
 * Attributes latency to specific pipeline stages for optimization.
 */
struct LatencyBreakdown {
    uint64_t network_ns;      // Exchange → CAL receive
    uint64_t parsing_ns;      // JSON parse time
    uint64_t queue_ns;        // CAL → ADE queue wait
    uint64_t analytics_ns;    // ADE processing
    uint64_t signal_ns;       // Signal generation
    uint64_t total_ns;        // Sum of all stages
    
    void record_total() noexcept {
        total_ns = network_ns + parsing_ns + queue_ns + analytics_ns + signal_ns;
    }
    
    // Identify bottleneck
    const char* bottleneck() const noexcept {
        uint64_t max_stage = 0;
        const char* name = "unknown";
        
        if (network_ns > max_stage) { max_stage = network_ns; name = "network"; }
        if (parsing_ns > max_stage) { max_stage = parsing_ns; name = "parsing"; }
        if (queue_ns > max_stage) { max_stage = queue_ns; name = "queue"; }
        if (analytics_ns > max_stage) { max_stage = analytics_ns; name = "analytics"; }
        if (signal_ns > max_stage) { max_stage = signal_ns; name = "signal"; }
        
        return name;
    }
};

} // namespace ade
} // namespace sage
