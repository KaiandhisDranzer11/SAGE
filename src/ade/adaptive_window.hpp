#pragma once

/**
 * SAGE Adaptive Window
 * Variance-scaled effective lookback for regime-responsive statistics
 * 
 * In calm markets: longer effective window → smoother, less reactive
 * In volatile markets: shorter effective window → faster adaptation
 * 
 * Uses volatility ratio to scale the effective sample weighting.
 */

#include <cstdint>
#include <algorithm>
#include "../core/constants.hpp"
#include "../core/compiler.hpp"

namespace sage {
namespace ade {

/**
 * Adaptive window statistics
 * 
 * Combines fixed rolling window with volatility-adaptive weighting.
 * Higher volatility → faster decay → more recent data weighted more.
 */
template<size_t MaxWindow = 128>
class AdaptiveWindow {
public:
    static_assert(MaxWindow > 0 && (MaxWindow & (MaxWindow - 1)) == 0,
                  "MaxWindow must be power of 2");
    
    /**
     * @param base_window Base window size in calm conditions
     * @param min_window Minimum effective window (fast adaptation)
     * @param vol_scale Volatility scaling factor (higher = more adaptive)
     */
    explicit AdaptiveWindow(size_t base_window = 64, 
                            size_t min_window = 16,
                            int64_t vol_scale = PRICE_SCALE) noexcept
        : base_window_(base_window)
        , min_window_(min_window)
        , vol_scale_(vol_scale)
        , count_(0)
        , head_(0)
        , sum_(0)
        , sum_sq_(0)
        , baseline_var_(0)
        , current_var_(0) {}
    
    /**
     * Update with new value
     */
    SAGE_HOT
    void update(int64_t value) noexcept {
        // Store in circular buffer
        size_t idx = head_ & (MaxWindow - 1);
        
        if (count_ >= MaxWindow) {
            // Remove oldest value
            sum_ -= buffer_[idx];
            sum_sq_ -= buffer_[idx] * buffer_[idx];
        }
        
        buffer_[idx] = value;
        sum_ += value;
        sum_sq_ += value * value;
        
        head_++;
        if (count_ < MaxWindow) count_++;
        
        // Update variance estimate
        update_variance();
    }
    
    /**
     * Get adaptive mean (weighted by effective window)
     */
    SAGE_ALWAYS_INLINE
    int64_t mean() const noexcept {
        size_t eff_window = effective_window();
        if (eff_window == 0) return 0;
        
        // Calculate mean over effective window
        int64_t eff_sum = 0;
        size_t start = (count_ > eff_window) ? count_ - eff_window : 0;
        
        for (size_t i = start; i < count_; ++i) {
            size_t idx = (head_ - count_ + i) & (MaxWindow - 1);
            eff_sum += buffer_[idx];
        }
        
        return eff_sum / static_cast<int64_t>(eff_window);
    }
    
    /**
     * Get current variance
     */
    SAGE_ALWAYS_INLINE
    int64_t variance() const noexcept {
        return current_var_;
    }
    
    /**
     * Get effective window size based on current volatility
     */
    size_t effective_window() const noexcept {
        if (count_ < min_window_) return count_;
        if (baseline_var_ <= 0) return base_window_;
        
        // Volatility ratio: current / baseline
        // Higher ratio → shorter window
        int64_t vol_ratio = (current_var_ * PRICE_SCALE) / baseline_var_;
        
        // Effective window = base / (1 + vol_ratio * scale)
        int64_t denominator = PRICE_SCALE + (vol_ratio * vol_scale_ / PRICE_SCALE);
        size_t eff = static_cast<size_t>(
            (base_window_ * PRICE_SCALE) / std::max(denominator, int64_t(1))
        );
        
        return std::clamp(eff, min_window_, base_window_);
    }
    
    /**
     * Get volatility ratio (current / baseline)
     */
    int64_t volatility_ratio() const noexcept {
        if (baseline_var_ <= 0) return PRICE_SCALE;
        return (current_var_ * PRICE_SCALE) / baseline_var_;
    }
    
    /**
     * Current sample count
     */
    size_t count() const noexcept { return count_; }
    
    /**
     * Is window warmed up?
     */
    bool is_ready() const noexcept { return count_ >= min_window_; }
    
    /**
     * Reset state
     */
    void reset() noexcept {
        count_ = 0;
        head_ = 0;
        sum_ = 0;
        sum_sq_ = 0;
        baseline_var_ = 0;
        current_var_ = 0;
    }

private:
    size_t base_window_;
    size_t min_window_;
    int64_t vol_scale_;
    
    size_t count_;
    size_t head_;
    int64_t buffer_[MaxWindow];
    int64_t sum_;
    int64_t sum_sq_;
    
    int64_t baseline_var_;  // Long-term variance estimate
    int64_t current_var_;   // Recent variance estimate
    
    void update_variance() noexcept {
        if (count_ < 2) return;
        
        // Current variance: E[X²] - E[X]²
        int64_t mean_val = sum_ / static_cast<int64_t>(count_);
        int64_t mean_sq = sum_sq_ / static_cast<int64_t>(count_);
        current_var_ = mean_sq - mean_val * mean_val;
        
        // Update baseline with slow EWMA (α ≈ 0.01)
        if (baseline_var_ == 0) {
            baseline_var_ = current_var_;
        } else {
            // baseline = 0.99 * baseline + 0.01 * current
            baseline_var_ = (baseline_var_ * 99 + current_var_) / 100;
        }
    }
};

} // namespace ade
} // namespace sage
