#pragma once

/**
 * SAGE Risk Limits
 * Production-grade risk configuration and branchless checking
 */

#include <cstdint>
#include "../core/compiler.hpp"

namespace sage {
namespace rme {

/**
 * Risk limit configuration
 */
struct RiskLimits {
    int64_t max_position_per_symbol;  // Max quantity per symbol
    int64_t max_total_exposure;       // Max total notional value
    int64_t max_daily_loss;           // Max loss per day (positive number)
    int64_t max_order_size;           // Max single order size
    double concentration_limit;        // Max % of portfolio in one symbol (0.0-1.0)
};

/**
 * Branchless risk checking utilities
 * All functions return 0 if OK, non-zero if violated
 */
namespace check {

/**
 * Check if position would exceed limit
 * Returns true if APPROVED
 */
SAGE_ALWAYS_INLINE
bool position_ok(int64_t position, int64_t limit) noexcept {
    // Branchless: uses conditional move internally
    return (position >= -limit) && (position <= limit);
}

/**
 * Check if exposure would exceed limit
 * Returns true if APPROVED
 */
SAGE_ALWAYS_INLINE
bool exposure_ok(int64_t exposure, int64_t limit) noexcept {
    return exposure <= limit;
}

/**
 * Check if daily loss is within limit
 * Returns true if APPROVED (pnl can be negative but must be > -limit)
 */
SAGE_ALWAYS_INLINE
bool pnl_ok(int64_t pnl, int64_t max_loss) noexcept {
    return pnl > -max_loss;
}

/**
 * Check if order size is within limit
 * Returns true if APPROVED
 */
SAGE_ALWAYS_INLINE
bool order_size_ok(int64_t size, int64_t max_size) noexcept {
    return std::abs(size) <= max_size;
}

/**
 * Combined branchless check (all conditions)
 * Returns true if ALL checks pass
 */
SAGE_HOT SAGE_ALWAYS_INLINE
bool all_checks_pass(
    int64_t new_position,
    int64_t position_limit,
    int64_t order_size,
    int64_t order_limit,
    int64_t total_exposure,
    int64_t exposure_limit,
    int64_t daily_pnl,
    int64_t loss_limit
) noexcept {
    // Use bitwise AND to combine all checks without branches
    // Compiler should optimize this to conditional moves
    bool ok = true;
    ok = ok && position_ok(new_position, position_limit);
    ok = ok && order_size_ok(order_size, order_limit);
    ok = ok && exposure_ok(total_exposure, exposure_limit);
    ok = ok && pnl_ok(daily_pnl, loss_limit);
    return ok;
}

} // namespace check

} // namespace rme
} // namespace sage
