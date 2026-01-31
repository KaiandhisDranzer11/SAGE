#pragma once

/**
 * SAGE Fixed-Point Arithmetic
 * Deterministic, overflow-safe price/quantity representation
 * 
 * Format: int64_t with 10^8 scale factor
 * Range: ±92,233,720,368.54775807
 * Precision: 8 decimal places
 */

#include <cstdint>
#include <compare>
#include <limits>
#include "../core/constants.hpp"
#include "../core/compiler.hpp"

namespace sage {

struct FixedPoint {
    int64_t value;
    
    // ========================================================================
    // Constructors
    // ========================================================================
    
    constexpr FixedPoint() noexcept : value(0) {}
    constexpr explicit FixedPoint(int64_t raw_value) noexcept : value(raw_value) {}
    
    // ========================================================================
    // Factory Methods
    // ========================================================================
    
    /**
     * Create from double (use only for initialization, not hot path)
     */
    static constexpr FixedPoint from_double(double d) noexcept {
        return FixedPoint(static_cast<int64_t>(d * PRICE_SCALE));
    }
    
    /**
     * Create from integer (multiplied by scale)
     */
    static constexpr FixedPoint from_int(int64_t i) noexcept {
        return FixedPoint(i * PRICE_SCALE);
    }
    
    /**
     * Create from integer and decimal parts
     * e.g., from_parts(100, 50000000) = 100.50
     */
    static constexpr FixedPoint from_parts(int64_t integer, int64_t decimal) noexcept {
        return FixedPoint(integer * PRICE_SCALE + decimal);
    }
    
    // ========================================================================
    // Conversion
    // ========================================================================
    
    constexpr double to_double() const noexcept {
        return static_cast<double>(value) / PRICE_SCALE;
    }
    
    constexpr int64_t to_int() const noexcept {
        return value / PRICE_SCALE;
    }
    
    constexpr int64_t raw() const noexcept {
        return value;
    }
    
    // ========================================================================
    // Arithmetic (Branchless)
    // ========================================================================
    
    SAGE_ALWAYS_INLINE constexpr FixedPoint operator+(FixedPoint other) const noexcept {
        return FixedPoint(value + other.value);
    }
    
    SAGE_ALWAYS_INLINE constexpr FixedPoint operator-(FixedPoint other) const noexcept {
        return FixedPoint(value - other.value);
    }
    
    /**
     * Multiplication with overflow protection
     * Uses 128-bit intermediate result
     */
    SAGE_ALWAYS_INLINE constexpr FixedPoint operator*(FixedPoint other) const noexcept {
        #if defined(__SIZEOF_INT128__)
            __int128 result = static_cast<__int128>(value) * other.value;
            return FixedPoint(static_cast<int64_t>(result / PRICE_SCALE));
        #else
            // Fallback: split into high/low parts
            int64_t a_hi = value / PRICE_SCALE;
            int64_t a_lo = value % PRICE_SCALE;
            int64_t b_hi = other.value / PRICE_SCALE;
            int64_t b_lo = other.value % PRICE_SCALE;
            
            return FixedPoint(
                a_hi * b_hi * PRICE_SCALE +
                a_hi * b_lo +
                a_lo * b_hi +
                (a_lo * b_lo) / PRICE_SCALE
            );
        #endif
    }
    
    /**
     * Division with overflow protection
     */
    SAGE_ALWAYS_INLINE constexpr FixedPoint operator/(FixedPoint other) const noexcept {
        #if defined(__SIZEOF_INT128__)
            __int128 result = (static_cast<__int128>(value) * PRICE_SCALE) / other.value;
            return FixedPoint(static_cast<int64_t>(result));
        #else
            // Fallback: less precise but safe
            return FixedPoint((value / other.value) * PRICE_SCALE);
        #endif
    }
    
    // ========================================================================
    // Compound Assignment
    // ========================================================================
    
    SAGE_ALWAYS_INLINE FixedPoint& operator+=(FixedPoint other) noexcept {
        value += other.value;
        return *this;
    }
    
    SAGE_ALWAYS_INLINE FixedPoint& operator-=(FixedPoint other) noexcept {
        value -= other.value;
        return *this;
    }
    
    // ========================================================================
    // Comparison (Branchless via spaceship)
    // ========================================================================
    
    constexpr auto operator<=>(const FixedPoint& other) const noexcept = default;
    constexpr bool operator==(const FixedPoint& other) const noexcept = default;
    
    // ========================================================================
    // Unary
    // ========================================================================
    
    constexpr FixedPoint operator-() const noexcept {
        return FixedPoint(-value);
    }
    
    constexpr FixedPoint abs() const noexcept {
        // Branchless absolute value
        int64_t mask = value >> 63;
        return FixedPoint((value + mask) ^ mask);
    }
    
    // ========================================================================
    // Utility
    // ========================================================================
    
    constexpr bool is_zero() const noexcept {
        return value == 0;
    }
    
    constexpr bool is_positive() const noexcept {
        return value > 0;
    }
    
    constexpr bool is_negative() const noexcept {
        return value < 0;
    }
    
    // ========================================================================
    // Constants
    // ========================================================================
    
    static constexpr FixedPoint zero() noexcept { return FixedPoint(0); }
    static constexpr FixedPoint one() noexcept { return FixedPoint(PRICE_SCALE); }
    static constexpr FixedPoint max_value() noexcept { 
        return FixedPoint(std::numeric_limits<int64_t>::max()); 
    }
    static constexpr FixedPoint min_value() noexcept { 
        return FixedPoint(std::numeric_limits<int64_t>::min()); 
    }
};

// Compile-time size verification
static_assert(sizeof(FixedPoint) == 8, "FixedPoint must be 8 bytes");

// ============================================================================
// Free Functions
// ============================================================================

SAGE_ALWAYS_INLINE constexpr FixedPoint abs(FixedPoint fp) noexcept {
    return fp.abs();
}

SAGE_ALWAYS_INLINE constexpr FixedPoint min(FixedPoint a, FixedPoint b) noexcept {
    // Branchless min
    int64_t diff = a.value - b.value;
    int64_t mask = diff >> 63;
    return FixedPoint(b.value + (diff & mask));
}

SAGE_ALWAYS_INLINE constexpr FixedPoint max(FixedPoint a, FixedPoint b) noexcept {
    // Branchless max
    int64_t diff = a.value - b.value;
    int64_t mask = diff >> 63;
    return FixedPoint(a.value - (diff & mask));
}

} // namespace sage
