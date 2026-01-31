#pragma once

#include <atomic>
#include <cstdint>
#include <chrono>

namespace sage {
namespace poe {

class OrderIDGenerator {
public:
    OrderIDGenerator() {
        // Capture startup timestamp (upper 32 bits)
        auto now = std::chrono::system_clock::now();
        auto epoch = now.time_since_epoch();
        startup_ts_ = std::chrono::duration_cast<std::chrono::seconds>(epoch).count();
        counter_.store(0);
    }
    
    // Generate globally unique, time-sortable order ID
    uint64_t generate() {
        uint32_t count = counter_.fetch_add(1, std::memory_order_relaxed);
        return (static_cast<uint64_t>(startup_ts_) << 32) | count;
    }

private:
    uint64_t startup_ts_;
    std::atomic<uint32_t> counter_;
};

} // namespace poe
} // namespace sage
