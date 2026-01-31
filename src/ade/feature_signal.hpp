#pragma once

/**
 * SAGE Feature Signal
 * Direction-agnostic feature extraction for strategy decoupling
 * 
 * ADE extracts statistical features without imposing trading beliefs.
 * Downstream components (MIND/RME) interpret features and decide strategy.
 * 
 * This allows:
 * - Multiple strategies on same features
 * - A/B testing of trading rules
 * - Strategy layer independent of analytics
 */

#include <cstdint>
#include "../types/fixed_point.hpp"
#include "../core/constants.hpp"

namespace sage {
namespace ade {

/**
 * Trading regime classification
 */
enum class MarketRegime : uint8_t {
    UNKNOWN = 0,
    LOW_VOL = 1,     // Calm market, mean-reversion likely
    NORMAL = 2,      // Normal conditions
    HIGH_VOL = 3,    // Elevated volatility
    REGIME_CHANGE = 4 // Volatility spike, signals unreliable
};

/**
 * Feature signal - direction-agnostic analytics output
 * 
 * Contains raw statistical features without trading direction.
 * Strategy layer interprets whether to mean-revert or trend-follow.
 */
struct FeatureSignal {
    // Identification
    uint64_t symbol_id;          ///< Symbol this signal refers to
    uint64_t timestamp_ns;       ///< Signal generation timestamp
    uint64_t sequence_id;        ///< Monotonic sequence number
    
    // Z-score features (scaled by PRICE_SCALE)
    int64_t z_score;             ///< Current price z-score (signed)
    int64_t z_score_ewma;        ///< EWMA-based z-score (faster response)
    
    // Volatility features
    int64_t volatility;          ///< Current volatility estimate
    int64_t volatility_percentile; ///< Percentile vs recent history (0-100 scaled)
    
    // Momentum features
    int64_t momentum_short;      ///< Short-term momentum (e.g., 10 ticks)
    int64_t momentum_long;       ///< Long-term momentum (e.g., 50 ticks)
    
    // Volume features
    int64_t volume_ratio;        ///< Current volume / average volume
    
    // Regime classification
    MarketRegime regime;         ///< Current market regime
    
    // Confidence metrics
    uint16_t samples;            ///< Number of samples used
    uint8_t quality;             ///< Signal quality (0-100)
    uint8_t flags;               ///< Additional flags
    
    // Helpers
    bool is_high_vol() const noexcept { 
        return regime == MarketRegime::HIGH_VOL || 
               regime == MarketRegime::REGIME_CHANGE; 
    }
    
    bool is_tradeable() const noexcept {
        return quality >= 50 && regime != MarketRegime::REGIME_CHANGE;
    }
    
    // Z-score convenience (unscaled)
    double z_score_double() const noexcept {
        return static_cast<double>(z_score) / PRICE_SCALE;
    }
};

/**
 * Strategy hint - optional interpretation layer
 * 
 * If ADE wants to suggest a direction, it can attach a hint.
 * This is advisory only; strategy layer makes final decision.
 */
struct StrategyHint {
    int8_t suggested_direction;  ///< +1 buy, -1 sell, 0 neutral
    int64_t confidence;          ///< Confidence in suggestion (0-PRICE_SCALE)
    uint8_t strategy_id;         ///< Which strategy generated hint
    
    static StrategyHint none() noexcept {
        return {0, 0, 0};
    }
    
    static StrategyHint mean_reversion(int64_t z_score) noexcept {
        // Mean reversion: buy low, sell high
        int8_t dir = (z_score > 0) ? -1 : 1;
        int64_t conf = std::abs(z_score);
        return {dir, conf, 1};  // strategy_id 1 = mean reversion
    }
    
    static StrategyHint momentum(int64_t momentum) noexcept {
        // Momentum: follow the trend
        int8_t dir = (momentum > 0) ? 1 : -1;
        int64_t conf = std::abs(momentum);
        return {dir, conf, 2};  // strategy_id 2 = momentum
    }
};

/**
 * Full analytics output combining features and optional hint
 */
struct AnalyticsOutput {
    FeatureSignal features;      ///< Direction-agnostic features
    StrategyHint hint;           ///< Optional strategy suggestion
    
    // Factory methods
    static AnalyticsOutput create(const FeatureSignal& f) noexcept {
        return {f, StrategyHint::none()};
    }
    
    static AnalyticsOutput with_mean_reversion(const FeatureSignal& f) noexcept {
        return {f, StrategyHint::mean_reversion(f.z_score)};
    }
    
    static AnalyticsOutput with_momentum(const FeatureSignal& f) noexcept {
        return {f, StrategyHint::momentum(f.momentum_short)};
    }
};

} // namespace ade
} // namespace sage
