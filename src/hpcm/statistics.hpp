#pragma once

#include <cstdint>

namespace sage {
namespace hpcm {

// Exponentially Weighted Moving Average
class EWMA {
public:
    explicit EWMA(double alpha) : alpha_(alpha), value_(0), initialized_(false) {}
    
    void update(int64_t new_value) {
        if (!initialized_) {
            value_ = new_value;
            initialized_ = true;
        } else {
            value_ = static_cast<int64_t>(alpha_ * new_value + (1.0 - alpha_) * value_);
        }
    }
    
    int64_t get() const { return value_; }

private:
    double alpha_;
    int64_t value_;
    bool initialized_;
};

// Simple volatility estimator (rolling standard deviation approximation)
class VolatilityEstimator {
public:
    VolatilityEstimator() : mean_(0), m2_(0), count_(0) {}
    
    void update(int64_t value) {
        count_++;
        int64_t delta = value - mean_;
        mean_ += delta / count_;
        int64_t delta2 = value - mean_;
        m2_ += delta * delta2;
    }
    
    int64_t variance() const {
        return count_ > 1 ? m2_ / (count_ - 1) : 0;
    }
    
    int64_t stddev() const {
        // Approximate sqrt for fixed point
        int64_t var = variance();
        if (var == 0) return 0;
        
        // Newton's method for integer sqrt
        int64_t x = var;
        int64_t y = (x + 1) / 2;
        while (y < x) {
            x = y;
            y = (x + var / x) / 2;
        }
        return x;
    }

private:
    int64_t mean_;
    int64_t m2_;
    int64_t count_;
};

} // namespace hpcm
} // namespace sage
