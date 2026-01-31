/**
 * SAGE Core Tests
 * Unit tests for core components
 */

#include <iostream>
#include <cassert>
#include <cmath>
#include <cstring>

#include "../src/core/compiler.hpp"
#include "../src/core/constants.hpp"
#include "../src/core/timing.hpp"
#include "../src/types/fixed_point.hpp"
#include "../src/types/sage_message.hpp"
#include "../src/infra/ring_buffer.hpp"

using namespace sage;

// ============================================================================
// Fixed Point Tests
// ============================================================================

void test_fixed_point_basic() {
    std::cout << "  Testing FixedPoint basic operations..." << std::endl;
    
    // Construction
    FixedPoint zero;
    assert(zero.raw() == 0);
    
    FixedPoint one = FixedPoint::one();
    assert(one.raw() == PRICE_SCALE);
    
    // From double
    FixedPoint price = FixedPoint::from_double(50000.12345678);
    assert(std::abs(price.to_double() - 50000.12345678) < 1e-7);
    
    // Addition
    FixedPoint a = FixedPoint::from_double(100.0);
    FixedPoint b = FixedPoint::from_double(200.0);
    FixedPoint c = a + b;
    assert(c.to_double() == 300.0);
    
    // Subtraction
    FixedPoint d = b - a;
    assert(d.to_double() == 100.0);
    
    // Multiplication
    FixedPoint e = FixedPoint::from_double(2.0);
    FixedPoint f = FixedPoint::from_double(3.0);
    FixedPoint g = e * f;
    assert(g.to_double() == 6.0);
    
    // Division
    FixedPoint h = FixedPoint::from_double(10.0);
    FixedPoint i = FixedPoint::from_double(2.0);
    FixedPoint j = h / i;
    assert(j.to_double() == 5.0);
    
    // Comparison
    assert(a < b);
    assert(b > a);
    assert(a != b);
    assert(a == FixedPoint::from_double(100.0));
    
    // Absolute value
    FixedPoint neg = FixedPoint::from_double(-50.0);
    assert(neg.abs().to_double() == 50.0);
    
    std::cout << "  FixedPoint basic: PASSED" << std::endl;
}

void test_fixed_point_overflow() {
    std::cout << "  Testing FixedPoint overflow protection..." << std::endl;
    
    // Large multiplication (would overflow without 128-bit intermediate)
    FixedPoint large1 = FixedPoint::from_double(1000000.0);
    FixedPoint large2 = FixedPoint::from_double(1000000.0);
    FixedPoint result = large1 * large2;
    
    // 10^6 * 10^6 = 10^12
    assert(std::abs(result.to_double() - 1e12) < 1e6);  // Allow some error due to fixed-point
    
    std::cout << "  FixedPoint overflow: PASSED" << std::endl;
}

// ============================================================================
// Message Tests
// ============================================================================

void test_sage_message() {
    std::cout << "  Testing SageMessage..." << std::endl;
    
    // Size verification
    static_assert(sizeof(SageMessage) == 64, "SageMessage must be 64 bytes");
    static_assert(alignof(SageMessage) == 64, "SageMessage must be 64-byte aligned");
    
    // Create market data message
    MarketData md;
    md.price = FixedPoint::from_double(50000.0);
    md.quantity = FixedPoint::from_double(0.1);
    md.symbol_id = 1;
    md.flags = 0x04;  // Trade
    md.exchange_id = 1;
    
    SageMessage msg = SageMessage::create_market_data(12345678, 1, md);
    
    assert(msg.timestamp_ns == 12345678);
    assert(msg.sequence_id == 1);
    assert(msg.msg_type == MessageType::MARKET_DATA);
    assert(msg.payload.market_data.price.to_double() == 50000.0);
    assert(msg.payload.market_data.quantity.to_double() == 0.1);
    assert(msg.is_valid());
    
    std::cout << "  SageMessage: PASSED" << std::endl;
}

// ============================================================================
// Ring Buffer Tests
// ============================================================================

void test_ring_buffer_basic() {
    std::cout << "  Testing RingBuffer basic operations..." << std::endl;
    
    RingBuffer<int, 16> rb;
    
    // Empty check
    assert(rb.empty_approx());
    assert(rb.size_approx() == 0);
    
    // Push
    assert(rb.try_push(42));
    assert(rb.size_approx() == 1);
    assert(!rb.empty_approx());
    
    // Pop
    int val = 0;
    assert(rb.try_pop(val));
    assert(val == 42);
    assert(rb.empty_approx());
    
    // Pop from empty
    assert(!rb.try_pop(val));
    
    std::cout << "  RingBuffer basic: PASSED" << std::endl;
}

void test_ring_buffer_full() {
    std::cout << "  Testing RingBuffer full condition..." << std::endl;
    
    RingBuffer<int, 16> rb;
    
    // Fill buffer
    for (int i = 0; i < 16; ++i) {
        assert(rb.try_push(i));
    }
    
    // Should be full
    assert(rb.full_approx());
    assert(!rb.try_push(99));  // Should fail
    
    // Pop one
    int val;
    assert(rb.try_pop(val));
    assert(val == 0);
    
    // Now can push again
    assert(rb.try_push(99));
    
    std::cout << "  RingBuffer full: PASSED" << std::endl;
}

void test_ring_buffer_wrap() {
    std::cout << "  Testing RingBuffer wraparound..." << std::endl;
    
    RingBuffer<int, 16> rb;
    
    // Push and pop many times to test wraparound
    for (int round = 0; round < 10; ++round) {
        for (int i = 0; i < 10; ++i) {
            assert(rb.try_push(round * 10 + i));
        }
        
        for (int i = 0; i < 10; ++i) {
            int val;
            assert(rb.try_pop(val));
            assert(val == round * 10 + i);
        }
    }
    
    assert(rb.empty_approx());
    
    std::cout << "  RingBuffer wraparound: PASSED" << std::endl;
}

void test_ring_buffer_batch() {
    std::cout << "  Testing RingBuffer batch operations..." << std::endl;
    
    RingBuffer<int, 64> rb;
    
    // Push 32 items
    for (int i = 0; i < 32; ++i) {
        rb.try_push(i);
    }
    
    // Batch pop
    int items[16];
    size_t count = rb.try_pop_batch(items, 16);
    assert(count == 16);
    
    for (int i = 0; i < 16; ++i) {
        assert(items[i] == i);
    }
    
    assert(rb.size_approx() == 16);
    
    std::cout << "  RingBuffer batch: PASSED" << std::endl;
}

// ============================================================================
// Timing Tests
// ============================================================================

void test_timing() {
    std::cout << "  Testing timing utilities..." << std::endl;
    
    // TSC read
    uint64_t tsc1 = timing::rdtsc();
    uint64_t tsc2 = timing::rdtsc();
    assert(tsc2 >= tsc1);  // Monotonic
    
    // RDTSCP
    uint64_t tsc3 = timing::rdtscp();
    assert(tsc3 > tsc1);
    
    // Monotonic clock
    uint64_t mono1 = timing::get_monotonic_ns();
    uint64_t mono2 = timing::get_monotonic_ns();
    assert(mono2 >= mono1);
    
    // Latency timer
    uint64_t latency;
    {
        timing::LatencyTimer timer(latency);
        // Do some work
        volatile int x = 0;
        for (int i = 0; i < 1000; ++i) x += i;
    }
    assert(latency > 0);
    
    std::cout << "  Timing: PASSED" << std::endl;
}

// ============================================================================
// Benchmark Tests
// ============================================================================

void benchmark_ring_buffer() {
    std::cout << "\n  Benchmarking RingBuffer latency..." << std::endl;
    
    constexpr size_t ITERATIONS = 1000000;
    RingBuffer<SageMessage, 1024> rb;
    
    SageMessage msg;
    msg.timestamp_ns = 0;
    msg.sequence_id = 0;
    msg.msg_type = MessageType::MARKET_DATA;
    
    // Warm up
    for (int i = 0; i < 1000; ++i) {
        rb.try_push(msg);
        rb.try_pop(msg);
    }
    
    // Benchmark push
    uint64_t push_start = timing::rdtscp();
    for (size_t i = 0; i < ITERATIONS; ++i) {
        msg.sequence_id = i;
        rb.try_push(msg);
        rb.try_pop(msg);
    }
    uint64_t push_end = timing::rdtscp();
    
    uint64_t cycles_per_op = (push_end - push_start) / ITERATIONS;
    
    // Approximate nanoseconds (assuming ~3GHz)
    double ns_per_op = cycles_per_op / 3.0;
    
    std::cout << "  Push+Pop: ~" << cycles_per_op << " cycles (~" 
              << ns_per_op << "ns @3GHz)" << std::endl;
    
    // Target: <50 cycles per push+pop
    if (cycles_per_op < 100) {
        std::cout << "  Performance: EXCELLENT" << std::endl;
    } else if (cycles_per_op < 200) {
        std::cout << "  Performance: GOOD" << std::endl;
    } else {
        std::cout << "  Performance: NEEDS OPTIMIZATION" << std::endl;
    }
}

void benchmark_fixed_point() {
    std::cout << "\n  Benchmarking FixedPoint arithmetic..." << std::endl;
    
    constexpr size_t ITERATIONS = 10000000;
    
    FixedPoint a = FixedPoint::from_double(50000.123);
    FixedPoint b = FixedPoint::from_double(0.00001);
    FixedPoint result = FixedPoint::zero();
    
    // Addition
    uint64_t start = timing::rdtscp();
    for (size_t i = 0; i < ITERATIONS; ++i) {
        result = result + a;
    }
    uint64_t add_cycles = (timing::rdtscp() - start) / ITERATIONS;
    
    // Multiplication
    result = FixedPoint::one();
    start = timing::rdtscp();
    for (size_t i = 0; i < ITERATIONS; ++i) {
        result = a * b;
    }
    uint64_t mul_cycles = (timing::rdtscp() - start) / ITERATIONS;
    
    std::cout << "  Addition: ~" << add_cycles << " cycles" << std::endl;
    std::cout << "  Multiplication: ~" << mul_cycles << " cycles" << std::endl;
    
    // Prevent optimization
    volatile double sink = result.to_double();
    (void)sink;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "====================================" << std::endl;
    std::cout << "SAGE Core Tests" << std::endl;
    std::cout << "====================================" << std::endl;
    
    std::cout << "\n[FixedPoint Tests]" << std::endl;
    test_fixed_point_basic();
    test_fixed_point_overflow();
    
    std::cout << "\n[SageMessage Tests]" << std::endl;
    test_sage_message();
    
    std::cout << "\n[RingBuffer Tests]" << std::endl;
    test_ring_buffer_basic();
    test_ring_buffer_full();
    test_ring_buffer_wrap();
    test_ring_buffer_batch();
    
    std::cout << "\n[Timing Tests]" << std::endl;
    test_timing();
    
    std::cout << "\n====================================" << std::endl;
    std::cout << "Benchmarks" << std::endl;
    std::cout << "====================================" << std::endl;
    
    benchmark_ring_buffer();
    benchmark_fixed_point();
    
    std::cout << "\n====================================" << std::endl;
    std::cout << "All tests PASSED!" << std::endl;
    std::cout << "====================================" << std::endl;
    
    return 0;
}
