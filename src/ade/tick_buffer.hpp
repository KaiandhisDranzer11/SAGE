#pragma once

#include <array>
#include <cstddef>
#include "../types/fixed_point.hpp"

namespace sage {
namespace ade {

template<size_t N>
class TickBuffer {
    static_assert((N & (N - 1)) == 0, "Buffer size must be power of 2");

public:
    TickBuffer() : pos_(0), count_(0) {}

    void push(FixedPoint price, FixedPoint qty) {
        prices_[pos_] = price;
        quantities_[pos_] = qty;
        pos_ = (pos_ + 1) & mask_;
        if (count_ < N) count_++;
    }

    FixedPoint get_price(size_t idx) const {
        return prices_[(pos_ - 1 - idx) & mask_];
    }

    FixedPoint get_qty(size_t idx) const {
        return quantities_[(pos_ - 1 - idx) & mask_];
    }

    size_t size() const { return count_; }
    bool is_full() const { return count_ == N; }

private:
    static constexpr size_t mask_ = N - 1;
    std::array<FixedPoint, N> prices_;
    std::array<FixedPoint, N> quantities_;
    size_t pos_;
    size_t count_;
};

} // namespace ade
} // namespace sage
