#pragma once

/**
 * SAGE Position Tracker
 * Production-grade position management with pre-allocation
 */

#include <array>
#include <atomic>
#include <cstdint>
#include <cmath>
#include "../core/compiler.hpp"
#include "../core/constants.hpp"
#include "../types/fixed_point.hpp"

namespace sage {
namespace rme {

constexpr size_t MAX_SYMBOLS = 256;

/**
 * Per-symbol position state
 */
struct alignas(CACHE_LINE_SIZE) Position {
    int64_t quantity;           // Positive = long, Negative = short
    int64_t avg_price_scaled;   // Average entry price (scaled)
    int64_t unrealized_pnl;     // Unrealized P&L
    int64_t realized_pnl;       // Realized P&L for the day
    uint64_t last_update_ns;    // Last update timestamp
    uint32_t trade_count;       // Number of trades today
    uint8_t  reserved[20];      // Pad to 64 bytes
};

static_assert(sizeof(Position) == 64, "Position must be cache-line aligned");

/**
 * Pre-allocated position tracker
 * No dynamic allocation, O(1) lookup by symbol index
 */
class PositionTracker {
public:
    PositionTracker() noexcept {
        reset();
    }
    
    /**
     * Reset all positions
     */
    void reset() noexcept {
        for (auto& pos : positions_) {
            pos = Position{};
        }
        total_exposure_.store(0, std::memory_order_relaxed);
        daily_pnl_.store(0, std::memory_order_relaxed);
    }
    
    /**
     * Update position by delta
     * Thread-safe for single writer
     */
    SAGE_HOT
    void update_position(uint64_t symbol_id, int64_t delta) noexcept {
        size_t idx = symbol_id & (MAX_SYMBOLS - 1);
        Position& pos = positions_[idx];
        
        int64_t old_qty = pos.quantity;
        int64_t new_qty = old_qty + delta;
        
        pos.quantity = new_qty;
        pos.trade_count++;
        
        // Update total exposure (atomic for readers)
        int64_t exposure_change = std::abs(new_qty) - std::abs(old_qty);
        total_exposure_.fetch_add(exposure_change, std::memory_order_release);
    }
    
    /**
     * Get position quantity
     */
    SAGE_ALWAYS_INLINE
    int64_t get_position(uint64_t symbol_id) const noexcept {
        size_t idx = symbol_id & (MAX_SYMBOLS - 1);
        return positions_[idx].quantity;
    }
    
    /**
     * Get full position info
     */
    const Position& get_position_info(uint64_t symbol_id) const noexcept {
        size_t idx = symbol_id & (MAX_SYMBOLS - 1);
        return positions_[idx];
    }
    
    /**
     * Get total exposure (thread-safe read)
     */
    SAGE_ALWAYS_INLINE
    int64_t get_total_exposure() const noexcept {
        return total_exposure_.load(std::memory_order_acquire);
    }
    
    /**
     * Get daily P&L (thread-safe read)
     */
    SAGE_ALWAYS_INLINE
    int64_t get_daily_pnl() const noexcept {
        return daily_pnl_.load(std::memory_order_acquire);
    }
    
    /**
     * Record realized P&L
     */
    void record_pnl(int64_t pnl) noexcept {
        daily_pnl_.fetch_add(pnl, std::memory_order_release);
    }

private:
    // Pre-allocated position array
    SAGE_CACHE_ALIGNED std::array<Position, MAX_SYMBOLS> positions_;
    
    // Atomic counters for thread-safe reading
    SAGE_CACHE_ALIGNED std::atomic<int64_t> total_exposure_{0};
    SAGE_CACHE_ALIGNED std::atomic<int64_t> daily_pnl_{0};
};

} // namespace rme
} // namespace sage
