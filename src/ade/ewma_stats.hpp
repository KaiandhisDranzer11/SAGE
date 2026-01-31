#pragma once

/**
 * SAGE EWMA Statistics
 * Exponentially Weighted Moving Average for responsive variance estimation
 * 
 * EWMA responds faster to regime changes than fixed rolling windows.
 * Uses recursive formula: EWMA_t = α * x_t + (1 - α) * EWMA_{t-1}
 * 
 * α (alpha) controls responsiveness:
 *   - Higher α (0.1-0.3) = faster response, more noise
 *   - Lower α (0.01-0.05) = slower response, smoother
 * 
 * Half-life conversion: α = 1 - exp(-ln(2) / half_life)
 * 
 * Fixed-point implementation for HFT determinism.
 */

#include <cstdint>
#include <cmath>
#include "../core/compiler.hpp"
#include "../core/constants.hpp"

namespace sage {
namespace ade {

/**
 * EWMA Statistics with fixed-point arithmetic
 * 
 * Provides O(1) update, O(1) query for mean and variance.
 * Uses scaled integer alpha for deterministic behavior.
 */
class EWMAStats {
public:
    // Alpha is scaled by ALPHA_SCALE for fixed-point math
    static constexpr int64_t ALPHA_SCALE = 10000;
    
    /**
     * Construct with half-life in ticks
     * 
     * @param half_life Number of ticks for weight to decay by half
     *                  Typical values: 20-100 ticks
     */
    explicit EWMAStats(int half_life = 50) noexcept 
        : alpha_(compute_alpha(half_life))
        , one_minus_alpha_(ALPHA_SCALE - alpha_)
        , ewma_mean_(0)
        , ewma_var_(0)
        , count_(0)
        , initialized_(false) {}
    
    /**
     * O(1) update with new value
     * 
     * Updates both mean and variance in a single pass.
     */
    SAGE_HOT
    void update(int64_t new_val) noexcept {
        if (!initialized_) {
            // First value - initialize directly
            ewma_mean_ = new_val * ALPHA_SCALE;
            ewma_var_ = 0;
            initialized_ = true;
            count_ = 1;
            return;
        }
        
        // EWMA mean: μ_t = α * x_t + (1 - α) * μ_{t-1}
        int64_t old_mean = ewma_mean_;
        ewma_mean_ = (alpha_ * new_val + one_minus_alpha_ * (old_mean / ALPHA_SCALE));
        
        // EWMA variance: σ²_t = (1 - α) * (σ²_{t-1} + α * (x_t - μ_{t-1})²)
        int64_t deviation = new_val - (old_mean / ALPHA_SCALE);
        int64_t deviation_sq = deviation * deviation;
        
        // Scale to prevent overflow
        int64_t scaled_dev_sq = deviation_sq / PRICE_SCALE;
        
        ewma_var_ = (one_minus_alpha_ * ewma_var_ + 
                     alpha_ * one_minus_alpha_ * scaled_dev_sq) / ALPHA_SCALE;
        
        count_++;
    }
    
    /**
     * Get EWMA mean (O(1))
     */
    SAGE_ALWAYS_INLINE
    int64_t mean() const noexcept {
        return initialized_ ? (ewma_mean_ / ALPHA_SCALE) : 0;
    }
    
    /**
     * Get EWMA variance (O(1))
     */
    SAGE_ALWAYS_INLINE
    int64_t variance() const noexcept {
        return ewma_var_ * PRICE_SCALE / ALPHA_SCALE;
    }
    
    /**
     * Get approximate standard deviation (O(1))
     * Uses Newton-Raphson integer sqrt
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
     * Get sample count
     */
    size_t count() const noexcept { return count_; }
    
    /**
     * Check if enough samples for meaningful stats
     */
    bool is_ready() const noexcept { return count_ >= 10; }
    
    /**
     * Reset state
     */
    void reset() noexcept {
        ewma_mean_ = 0;
        ewma_var_ = 0;
        count_ = 0;
        initialized_ = false;
    }
    
    /**
     * Get current alpha (scaled)
     */
    int64_t alpha_scaled() const noexcept { return alpha_; }
    
    /**
     * Compute alpha from half-life
     * α = 1 - exp(-ln(2) / half_life)
     */
    static int64_t compute_alpha(int half_life) noexcept {
        if (half_life <= 0) return ALPHA_SCALE / 10;  // Default 0.1
        
        // α = 1 - exp(-0.693 / half_life)
        // For integer math, use approximation: α ≈ 0.693 / half_life for small α
        double alpha = 1.0 - std::exp(-0.693147 / half_life);
        return static_cast<int64_t>(alpha * ALPHA_SCALE);
    }

private:
    int64_t alpha_;           // Scaled alpha (0 to ALPHA_SCALE)
    int64_t one_minus_alpha_; // Precomputed (1 - alpha)
    int64_t ewma_mean_;       // Scaled EWMA of values
    int64_t ewma_var_;        // Scaled EWMA of variance
    size_t count_;            // Sample count
    bool initialized_;        // First value seen
};

/**
 * Volatility regime detector
 * 
 * Tracks variance-of-variance to detect regime changes.
 * When vol-of-vol spikes, signals should be gated or scaled.
 */
class VolRegimeDetector {
public:
    explicit VolRegimeDetector(int half_life = 100) noexcept
        : vol_ewma_(half_life)
        , vol_of_vol_ewma_(half_life * 2)
        , regime_threshold_(2 * PRICE_SCALE)  // 2x normal vol
        , last_vol_(0) {}
    
    /**
     * Update with new variance observation
     * 
     * @param variance Current variance estimate
     * @return true if regime change detected
     */
    SAGE_HOT
    bool update(int64_t variance) noexcept {
        // Track volatility (sqrt of variance)
        int64_t vol = isqrt(variance);
        
        // Update EWMA of volatility
        vol_ewma_.update(vol);
        
        // Update vol-of-vol (variance of volatility changes)
        if (last_vol_ > 0) {
            int64_t vol_change = std::abs(vol - last_vol_);
            vol_of_vol_ewma_.update(vol_change);
        }
        last_vol_ = vol;
        
        return is_regime_change();
    }
    
    /**
     * Check if currently in regime change
     * 
     * Returns true if vol-of-vol is significantly elevated.
     */
    bool is_regime_change() const noexcept {
        if (!vol_of_vol_ewma_.is_ready()) return false;
        
        int64_t vol_of_vol = vol_of_vol_ewma_.mean();
        int64_t normal_vol = vol_ewma_.mean();
        
        if (normal_vol <= 0) return false;
        
        // Regime change if vol-of-vol > threshold * normal_vol
        return vol_of_vol > (regime_threshold_ * normal_vol / PRICE_SCALE);
    }
    
    /**
     * Get current volatility estimate
     */
    int64_t current_vol() const noexcept { return vol_ewma_.mean(); }
    
    /**
     * Get volatility-of-volatility
     */
    int64_t vol_of_vol() const noexcept { return vol_of_vol_ewma_.mean(); }
    
    /**
     * Set regime detection threshold
     * 
     * @param multiplier Threshold as multiple of normal vol (scaled by PRICE_SCALE)
     */
    void set_threshold(int64_t multiplier) noexcept {
        regime_threshold_ = multiplier;
    }

private:
    EWMAStats vol_ewma_;        // EWMA of volatility
    EWMAStats vol_of_vol_ewma_; // EWMA of vol changes
    int64_t regime_threshold_;  // Threshold for regime detection
    int64_t last_vol_;          // Previous volatility value
    
    // Integer square root
    static int64_t isqrt(int64_t n) noexcept {
        if (n <= 0) return 0;
        int64_t x = n;
        int64_t y = (x + 1) / 2;
        while (y < x) {
            x = y;
            y = (x + n / x) / 2;
        }
        return x;
    }
};

} // namespace ade
} // namespace sage
