#pragma once

/**
 * SAGE Rolling Statistics
 * O(1) incremental statistics for HFT
 * 
 * Algorithm: Maintains running sum and sum-of-squares for O(1) mean and variance.
 * Uses Newton-Raphson integer sqrt for stddev approximation.
 * 
 * KNOWN LIMITATIONS:
 * - Static window size doesn't adapt to market conditions
 *   → Overreacts in calm periods, underreacts in fast markets
 * - Assumes stationary distribution (mean/variance stable over window)
 *   → During regime changes, statistics lag reality
 * - Simple variance, not EWMA → Slow to react to volatility clustering
 * 
 * FUTURE IMPROVEMENTS:
 * - EWMA variance (exponentially weighted) for faster regime response
 * - Adaptive window sizing based on recent variance-of-variance
 * - Winsorized updates to limit outlier impact
 * 
 * @tparam N Window size (must be power of 2 for efficient modulo)
 */


#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include "../core/compiler.hpp"
#include "../types/fixed_point.hpp"

namespace sage {
namespace ade {

/**
 * Rolling statistics with O(1) mean and approximate stddev
 * Uses Welford's online algorithm for variance
 */
template<size_t N>
class RollingStats {
    static_assert((N & (N - 1)) == 0, "Window size must be power of 2");

public:
    RollingStats() noexcept : pos_(0), count_(0), sum_(0), sum_sq_(0) {
        buffer_.fill(0);
    }

    /**
     * O(1) update with new value
     */
    SAGE_HOT
    void update(int64_t new_val) noexcept {
        if (count_ == N) {
            // Remove oldest value
            int64_t old_val = buffer_[pos_];
            sum_ -= old_val;
            sum_sq_ -= old_val * old_val;
        } else {
            count_++;
        }
        
        buffer_[pos_] = new_val;
        sum_ += new_val;
        sum_sq_ += new_val * new_val;
        pos_ = (pos_ + 1) & mask_;
    }

    /**
     * Get mean (O(1))
     */
    SAGE_ALWAYS_INLINE
    int64_t mean() const noexcept {
        return count_ > 0 ? sum_ / static_cast<int64_t>(count_) : 0;
    }

    /**
     * Get variance (O(1), approximate due to integer math)
     */
    SAGE_ALWAYS_INLINE
    int64_t variance() const noexcept {
        if (count_ < 2) return 0;
        int64_t n = static_cast<int64_t>(count_);
        int64_t mean_val = mean();
        return (sum_sq_ / n) - (mean_val * mean_val);
    }

    /**
     * Get approximate standard deviation (O(1))
     * Uses integer square root approximation
     */
    SAGE_ALWAYS_INLINE
    int64_t stddev_approx() const noexcept {
        int64_t var = variance();
        if (var <= 0) return 0;
        
        // Newton-Raphson integer sqrt (3 iterations)
        int64_t x = var;
        int64_t y = (x + 1) / 2;
        while (y < x) {
            x = y;
            y = (x + var / x) / 2;
        }
        return x;
    }

    /**
     * Get sum (O(1))
     */
    int64_t sum() const noexcept { return sum_; }
    
    /**
     * Get count (O(1))
     */
    size_t count() const noexcept { return count_; }
    
    /**
     * Check if buffer is full
     */
    bool is_full() const noexcept { return count_ == N; }
    
    /**
     * Reset all state
     */
    void reset() noexcept {
        pos_ = 0;
        count_ = 0;
        sum_ = 0;
        sum_sq_ = 0;
        buffer_.fill(0);
    }

private:
    static constexpr size_t mask_ = N - 1;
    
    SAGE_CACHE_ALIGNED std::array<int64_t, N> buffer_;
    size_t pos_;
    size_t count_;
    int64_t sum_;
    int64_t sum_sq_;  // Sum of squares for variance
};

} // namespace ade
} // namespace sage
