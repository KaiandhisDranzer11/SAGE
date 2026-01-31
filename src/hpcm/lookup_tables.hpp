#pragma once

#include <array>
#include <cmath>
#include <numbers>

namespace sage {
namespace hpcm {

// Pre-computed lookup tables for fast math operations
class LookupTables {
public:
    static constexpr size_t TABLE_SIZE = 65536; // 2^16 entries
    
    // Initialize tables at startup (call once)
    static void initialize() {
        static bool initialized = false;
        if (initialized) return;
        
        for (size_t i = 0; i < TABLE_SIZE; ++i) {
            double angle = (2.0 * std::numbers::pi * i) / TABLE_SIZE;
            sin_table_[i] = std::sin(angle);
            cos_table_[i] = std::cos(angle);
        }
        
        initialized = true;
    }
    
    // Fast sin lookup (angle in [0, 2π] mapped to [0, 65535])
    static double sin_lookup(uint16_t angle_index) {
        return sin_table_[angle_index];
    }
    
    static double cos_lookup(uint16_t angle_index) {
        return cos_table_[angle_index];
    }

private:
    static inline std::array<double, TABLE_SIZE> sin_table_;
    static inline std::array<double, TABLE_SIZE> cos_table_;
};

} // namespace hpcm
} // namespace sage
