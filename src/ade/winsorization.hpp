#pragma once

/**
 * SAGE Winsorization
 * Outlier-resistant statistics for robust signal generation
 * 
 * Winsorization caps extreme values to limit their influence on statistics.
 * This prevents single outliers from dominating mean/variance estimates.
 * 
 * Methods:
 * - Percentile clipping: values beyond Nth percentile are clamped
 * - MAD-based: values beyond k * MAD from median are clamped
 * - Z-score capping: limit individual z-score contributions
 */

#include <cstdint>
#include <algorithm>
#include <cmath>
#include "../core/constants.hpp"
#include "../core/compiler.hpp"

namespace sage {
namespace ade {

/**
 * Z-score capper
 * 
 * Limits z-score contribution per tick to prevent outlier dominance.
 * Useful when single extreme prices shouldn't dominate signals.
 */
class ZScoreCapper {
public:
    /**
     * @param max_z Maximum absolute z-score (scaled by PRICE_SCALE)
     *              Default 3.0 → values beyond 3σ are capped
     */
    explicit ZScoreCapper(int64_t max_z = 3 * PRICE_SCALE) noexcept
        : max_z_(max_z) {}
    
    /**
     * Cap z-score to maximum bounds
     * 
     * @param z_score Raw z-score (scaled)
     * @return Capped z-score in [-max_z, +max_z]
     */
    SAGE_ALWAYS_INLINE
    int64_t cap(int64_t z_score) const noexcept {
        if (z_score > max_z_) return max_z_;
        if (z_score < -max_z_) return -max_z_;
        return z_score;
    }
    
    /**
     * Check if value was capped
     */
    SAGE_ALWAYS_INLINE
    bool is_outlier(int64_t z_score) const noexcept {
        return std::abs(z_score) > max_z_;
    }
    
    /**
     * Get capping threshold
     */
    int64_t max_z() const noexcept { return max_z_; }
    
    /**
     * Set capping threshold
     */
    void set_max_z(int64_t max_z) noexcept { max_z_ = max_z; }

private:
    int64_t max_z_;
};

/**
 * Winsorized rolling statistics
 * 
 * Rolling statistics with outlier capping for robust estimation.
 * Values beyond threshold are clamped before contributing to mean/var.
 */
template<size_t Window = 64>
class WinsorizedStats {
public:
    static_assert(Window > 0 && (Window & (Window - 1)) == 0,
                  "Window must be power of 2");
    
    /**
     * @param winsor_pct Winsorization percentile (0-50)
     *                   5 means clip top/bottom 5%
     */
    explicit WinsorizedStats(int winsor_pct = 5) noexcept
        : winsor_pct_(std::clamp(winsor_pct, 0, 50))
        , count_(0)
        , head_(0)
        , sum_(0)
        , sum_sq_(0) {}
    
    /**
     * Update with new value
     */
    SAGE_HOT
    void update(int64_t value) noexcept {
        size_t idx = head_ & (Window - 1);
        
        if (count_ >= Window) {
            // Remove oldest
            int64_t old_val = buffer_[idx];
            sum_ -= old_val;
            sum_sq_ -= old_val * old_val;
        }
        
        // Store raw value
        buffer_[idx] = value;
        head_++;
        if (count_ < Window) count_++;
        
        // Recalculate winsorized sum (O(n) but n is small)
        recalculate_sums();
    }
    
    /**
     * Get winsorized mean
     */
    int64_t mean() const noexcept {
        if (count_ == 0) return 0;
        return sum_ / static_cast<int64_t>(count_);
    }
    
    /**
     * Get winsorized variance
     */
    int64_t variance() const noexcept {
        if (count_ < 2) return 0;
        int64_t mean_val = mean();
        int64_t mean_sq = sum_sq_ / static_cast<int64_t>(count_);
        return mean_sq - mean_val * mean_val;
    }
    
    /**
     * Get approximate stddev (integer sqrt)
     */
    int64_t stddev_approx() const noexcept {
        int64_t var = variance();
        if (var <= 0) return 0;
        
        // Newton-Raphson integer sqrt
        int64_t x = var;
        int64_t y = (x + 1) / 2;
        while (y < x) {
            x = y;
            y = (x + var / x) / 2;
        }
        return x;
    }
    
    /**
     * Get current outlier count
     */
    size_t outlier_count() const noexcept { return outlier_count_; }
    
    /**
     * Sample count
     */
    size_t count() const noexcept { return count_; }
    
    /**
     * Is ready for signals?
     */
    bool is_ready() const noexcept { return count_ >= Window / 2; }

private:
    int winsor_pct_;
    size_t count_;
    size_t head_;
    int64_t buffer_[Window];
    int64_t sum_;
    int64_t sum_sq_;
    size_t outlier_count_{0};
    
    void recalculate_sums() noexcept {
        if (count_ == 0) return;
        
        // Copy to temp array for sorting
        int64_t sorted[Window];
        for (size_t i = 0; i < count_; ++i) {
            size_t idx = (head_ - count_ + i) & (Window - 1);
            sorted[i] = buffer_[idx];
        }
        
        // Simple insertion sort (small n)
        for (size_t i = 1; i < count_; ++i) {
            int64_t key = sorted[i];
            size_t j = i;
            while (j > 0 && sorted[j - 1] > key) {
                sorted[j] = sorted[j - 1];
                j--;
            }
            sorted[j] = key;
        }
        
        // Calculate clip indices
        size_t clip_count = (count_ * winsor_pct_) / 100;
        int64_t low_clip = sorted[clip_count];
        int64_t high_clip = sorted[count_ - 1 - clip_count];
        
        // Calculate winsorized sums
        sum_ = 0;
        sum_sq_ = 0;
        outlier_count_ = 0;
        
        for (size_t i = 0; i < count_; ++i) {
            int64_t val = sorted[i];
            
            // Clamp to bounds
            if (val < low_clip) {
                val = low_clip;
                outlier_count_++;
            } else if (val > high_clip) {
                val = high_clip;
                outlier_count_++;
            }
            
            sum_ += val;
            sum_sq_ += val * val;
        }
    }
};

} // namespace ade
} // namespace sage
