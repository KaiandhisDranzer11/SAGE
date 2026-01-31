#pragma once

/**
 * SAGE CAL Validator
 * Market data validation with symbol mapping protection
 * 
 * SYMBOL ALIASING PROTECTION:
 * External symbol IDs must be validated against a known symbol table.
 * The bitmask lookup `symbol_id & (MAX_SYMBOLS - 1)` in ADE will alias
 * different symbols if external IDs are not controlled.
 * 
 * Use validate_symbol_id() to reject unknown symbols at ingress.
 */

#include <cmath>
#include <limits>
#include <unordered_set>
#include "../types/sage_message.hpp"

namespace sage {
namespace cal {

// Maximum valid symbol ID (should match ADE's MAX_SYMBOLS)
constexpr uint64_t MAX_VALID_SYMBOL_ID = 256;

enum class ValidationStatus {
    ACCEPT,
    REJECT,
    WARN
};

struct ValidationResult {
    ValidationStatus status;
    const char* reason;
};

/**
 * Market data and symbol validator
 * 
 * Provides stateless validation for incoming market data.
 * Symbol validation prevents cross-symbol aliasing in downstream analytics.
 */
class Validator {
public:
    // Constants for validation
    static constexpr int64_t MAX_PRICE_SPIKE_PERCENT = 10; // 10% move is suspicious
    
    /**
     * Validate symbol ID is within acceptable range
     * 
     * CRITICAL: Prevents symbol aliasing in ADE's bitmask lookup.
     * Unknown symbols should be rejected at ingress, not silently aliased.
     * 
     * @param symbol_id External symbol identifier
     * @return true if symbol_id < MAX_VALID_SYMBOL_ID
     */
    static bool validate_symbol_id(uint64_t symbol_id) noexcept {
        if (symbol_id >= MAX_VALID_SYMBOL_ID) {
            // Symbol would alias with another in ADE's bitmask lookup
            // This is a data integrity issue, not a performance issue
            return false;
        }
        return true;
    }
    
    /**
     * Validate market data message
     * 
     * Performs stateless validation on incoming market data.
     * Does NOT validate symbol (use validate_symbol_id separately at startup).
     */
    static ValidationResult validate_market_data(const MarketData& data) noexcept {
        // Symbol ID range check (prevents aliasing)
        if (data.symbol_id >= MAX_VALID_SYMBOL_ID) {
            return {ValidationStatus::REJECT, "Symbol ID out of range"};
        }
        
        // Price check
        if (data.price.raw() <= 0) {
            return {ValidationStatus::REJECT, "Price <= 0"};
        }

        // Qty check
        if (data.quantity.raw() <= 0) {
            return {ValidationStatus::REJECT, "Qty <= 0"};
        }

        // Note: Comparison against previous price for spike detection
        // requires state, which Validator doesn't have.
        // Stateful spike detection should be done at a higher layer.

        return {ValidationStatus::ACCEPT, nullptr};
    }
    
    /**
     * Check if double input from JSON is valid before conversion
     * 
     * Guards against NaN, Inf, and negative values before FixedPoint conversion.
     */
    static bool is_safe_float(double d) noexcept {
        return std::isfinite(d) && d > 0.0;
    }
    
    /**
     * Validate price is within reasonable bounds
     * 
     * Prevents overflow in fixed-point arithmetic and catches
     * obvious data corruption (e.g., $0 or $1 trillion prices).
     */
    static bool is_valid_price(double price, double min_price = 0.0001, 
                               double max_price = 1e12) noexcept {
        return std::isfinite(price) && price >= min_price && price <= max_price;
    }
    
    /**
     * Validate quantity is within reasonable bounds
     */
    static bool is_valid_quantity(double qty, double min_qty = 1e-8,
                                  double max_qty = 1e12) noexcept {
        return std::isfinite(qty) && qty >= min_qty && qty <= max_qty;
    }
};

/**
 * Symbol table for validated symbols
 * 
 * Provides O(1) lookup for symbol validation at ingress.
 * Build at startup from configuration, then use for every message.
 * 
 * USAGE:
 *   SymbolTable symbols;
 *   symbols.add_symbol(1, "BTCUSD");
 *   symbols.add_symbol(2, "ETHUSD");
 *   
 *   // In hot path:
 *   if (!symbols.is_valid(msg.symbol_id)) reject(msg);
 */
class SymbolTable {
public:
    /**
     * Register a valid symbol ID
     */
    void add_symbol(uint64_t symbol_id, const char* /* symbol_name */ = nullptr) {
        if (symbol_id < MAX_VALID_SYMBOL_ID) {
            valid_symbols_.insert(symbol_id);
        }
    }
    
    /**
     * Check if symbol ID is registered
     * O(1) average case
     */
    bool is_valid(uint64_t symbol_id) const noexcept {
        return valid_symbols_.find(symbol_id) != valid_symbols_.end();
    }
    
    /**
     * Get count of registered symbols
     */
    size_t count() const noexcept { return valid_symbols_.size(); }
    
    /**
     * Clear all registered symbols
     */
    void clear() { valid_symbols_.clear(); }

private:
    std::unordered_set<uint64_t> valid_symbols_;
};

} // namespace cal
} // namespace sage
