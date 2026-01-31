#pragma once

#include <atomic>
#include <cstdint>

namespace sage {
namespace rme {

enum class CircuitBreakerReason {
    NONE,
    HIGH_ERROR_RATE,
    LATENCY_SPIKE,
    DAILY_LOSS_BREACH,
    MANUAL_HALT
};

class CircuitBreaker {
public:
    CircuitBreaker() : tripped_(false), reason_(CircuitBreakerReason::NONE) {}
    
    void trip(CircuitBreakerReason reason) {
        if (!tripped_.exchange(true)) {
            reason_ = reason;
            // Log critical alert
        }
    }
    
    void reset() {
        tripped_.store(false);
        reason_ = CircuitBreakerReason::NONE;
    }
    
    bool is_tripped() const {
        return tripped_.load(std::memory_order_relaxed);
    }
    
    CircuitBreakerReason get_reason() const {
        return reason_;
    }

private:
    std::atomic<bool> tripped_;
    CircuitBreakerReason reason_;
};

} // namespace rme
} // namespace sage
