#pragma once

#include "../types/fixed_point.hpp"

namespace sage {
namespace ade {

class Normalizer {
public:
    // Normalize value to [-1.0, 1.0] range using min/max
    static int64_t normalize(int64_t value, int64_t min_val, int64_t max_val) {
        if (max_val == min_val) return 0;
        
        // Scale to [-1, 1] represented as fixed point
        // normalized = 2 * (value - min) / (max - min) - 1
        int64_t range = max_val - min_val;
        int64_t shifted = value - min_val;
        
        // Return as scaled integer (multiply by PRICE_SCALE for fixed point)
        return (2 * shifted * PRICE_SCALE / range) - PRICE_SCALE;
    }

    // Z-score normalization: (x - mean) / stddev
    static int64_t z_score(int64_t value, int64_t mean, int64_t stddev) {
        if (stddev == 0) return 0;
        return ((value - mean) * PRICE_SCALE) / stddev;
    }
};

} // namespace ade
} // namespace sage
