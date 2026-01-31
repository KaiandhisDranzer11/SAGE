/**
 * SAGE RME - Risk Management Engine
 * Production-grade real-time risk checking
 */

#include <iostream>
#include <thread>
#include <array>
#include <atomic>

#include "../core/compiler.hpp"
#include "../core/constants.hpp"
#include "../core/timing.hpp"
#include "../core/cpu_affinity.hpp"
#include "../core/shutdown.hpp"
#include "../infra/ring_buffer.hpp"
#include "../types/sage_message.hpp"
#include "position_tracker.hpp"
#include "risk_limits.hpp"
#include "circuit_breaker.hpp"

using namespace sage;

// ============================================================================
// Configuration
// ============================================================================

constexpr size_t MAX_SYMBOLS = 256;

// Risk limits (configurable at startup)
static rme::RiskLimits g_limits{
    .max_position_per_symbol = 1000000,     // $1M per symbol
    .max_total_exposure = 10000000,          // $10M total
    .max_daily_loss = 100000,                // $100K daily loss
    .max_order_size = 50000                  // $50K per order
};

// ============================================================================
// Global State
// ============================================================================

// Ring buffers
static RingBuffer<SageMessage, 65536> g_ade_to_rme_buffer;
static RingBuffer<SageMessage, 65536> g_rme_to_poe_buffer;

// Position tracker (pre-allocated)
static rme::PositionTracker g_position_tracker;

// Circuit breaker
static rme::CircuitBreaker g_circuit_breaker;

// Metrics
static std::atomic<uint64_t> g_signals_received{0};
static std::atomic<uint64_t> g_orders_approved{0};
static std::atomic<uint64_t> g_orders_rejected{0};
static std::atomic<uint64_t> g_total_latency_ns{0};

// Sequence counter
static uint64_t g_sequence = 0;

// TSC calibrator
static timing::TSCCalibrator g_tsc_calibrator;

// ============================================================================
// Branchless Risk Checks
// ============================================================================

/**
 * Branchless check if value exceeds limit
 * Returns 0 if OK, non-zero if exceeded
 */
SAGE_ALWAYS_INLINE
static int64_t exceeds_limit(int64_t value, int64_t limit) noexcept {
    // Returns positive if value > limit (exceeded)
    return (value - limit) >> 63 ^ 0;  // 0 if exceeded, -1 if ok
}

/**
 * Fast risk check combining all conditions
 * Returns true if order is APPROVED
 */
SAGE_HOT SAGE_ALWAYS_INLINE
static bool check_risk_fast(uint64_t symbol_id, int64_t order_value) noexcept {
    // Circuit breaker check - fastest path
    if (g_circuit_breaker.is_tripped()) [[unlikely]] {
        return false;
    }
    
    // Get current position
    int64_t current_position = g_position_tracker.get_position(symbol_id);
    int64_t new_position = current_position + order_value;
    
    // Check position limit (branchless)
    bool position_ok = std::abs(new_position) <= g_limits.max_position_per_symbol;
    
    // Check order size limit
    bool size_ok = std::abs(order_value) <= g_limits.max_order_size;
    
    // Check total exposure
    int64_t total_exposure = g_position_tracker.get_total_exposure();
    bool exposure_ok = total_exposure + std::abs(order_value) <= g_limits.max_total_exposure;
    
    // Check daily PnL
    bool pnl_ok = g_position_tracker.get_daily_pnl() > -g_limits.max_daily_loss;
    
    return position_ok && size_ok && exposure_ok && pnl_ok;
}

// ============================================================================
// Hot Path Processing
// ============================================================================

SAGE_HOT SAGE_FLATTEN
static void process_signal(const SageMessage& msg) noexcept {
    const uint64_t start_tsc = timing::rdtsc();
    
    const auto& signal = msg.payload.signal;
    
    g_signals_received.fetch_add(1, std::memory_order_relaxed);
    
    // Calculate order value based on signal
    int64_t order_value = signal.confidence.raw() * signal.direction;
    
    // Fast risk check
    if (!check_risk_fast(signal.symbol_id, order_value)) [[unlikely]] {
        g_orders_rejected.fetch_add(1, std::memory_order_relaxed);
        
        // Track latency even for rejections
        const uint64_t latency_tsc = timing::rdtsc() - start_tsc;
        g_total_latency_ns.fetch_add(
            g_tsc_calibrator.tsc_to_ns(latency_tsc),
            std::memory_order_relaxed
        );
        return;
    }
    
    // Create order request
    OrderRequest order;
    order.order_id = ++g_sequence;
    order.symbol_id = signal.symbol_id;
    order.price = FixedPoint::zero();  // Market order
    order.quantity = signal.confidence;
    order.side = signal.direction;
    order.order_type = 1;  // Market
    order.time_in_force = 1;  // IOC
    
    SageMessage out_msg;
    out_msg.timestamp_ns = timing::get_monotonic_ns();
    out_msg.sequence_id = g_sequence;
    out_msg.msg_type = MessageType::ORDER_REQUEST;
    out_msg.payload.order = order;
    
    // Update position (before sending)
    g_position_tracker.update_position(signal.symbol_id, order_value);
    
    // Push to POE
    if (g_rme_to_poe_buffer.try_push(out_msg)) {
        g_orders_approved.fetch_add(1, std::memory_order_relaxed);
    }
    
    // Track latency
    const uint64_t latency_tsc = timing::rdtsc() - start_tsc;
    g_total_latency_ns.fetch_add(
        g_tsc_calibrator.tsc_to_ns(latency_tsc),
        std::memory_order_relaxed
    );
}

// ============================================================================
// Heartbeat Thread
// ============================================================================

static void heartbeat_thread() {
    cpu::pin_to_core(CORE_OS);
    
    while (!ShutdownManager::instance().is_shutdown_requested()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        uint64_t received = g_signals_received.load();
        uint64_t approved = g_orders_approved.load();
        uint64_t rejected = g_orders_rejected.load();
        uint64_t total_latency = g_total_latency_ns.load();
        
        double avg_latency_ns = (received > 0) ? 
            static_cast<double>(total_latency) / received : 0.0;
        
        std::cout << "[RME] Stats: signals=" << received
                  << " approved=" << approved
                  << " rejected=" << rejected
                  << " avg_latency=" << avg_latency_ns << "ns"
                  << " exposure=" << g_position_tracker.get_total_exposure()
                  << " pnl=" << g_position_tracker.get_daily_pnl()
                  << std::endl;
        
        // Check for circuit breaker conditions
        if (g_position_tracker.get_daily_pnl() < -g_limits.max_daily_loss) {
            std::cout << "[RME] CIRCUIT BREAKER: Daily loss limit exceeded!" << std::endl;
            g_circuit_breaker.trip("Daily loss limit");
        }
    }
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "[RME] Starting Risk Management Engine..." << std::endl;
    std::cout << "[RME] Limits: max_position=" << g_limits.max_position_per_symbol
              << " max_exposure=" << g_limits.max_total_exposure
              << " max_daily_loss=" << g_limits.max_daily_loss
              << std::endl;
    
    // Pin to designated core
    if (cpu::pin_to_core(CORE_RME) == 0) {
        std::cout << "[RME] Pinned to core " << CORE_RME << std::endl;
    }
    
    // Try real-time priority (highest for risk)
    if (cpu::set_realtime_priority(80) == 0) {
        std::cout << "[RME] Real-time priority set (80)" << std::endl;
    }
    
    ShutdownManager::instance().install_signal_handlers();
    
    // Start heartbeat
    std::thread hb_thread(heartbeat_thread);
    
    std::cout << "[RME] Entering main loop..." << std::endl;
    
    // Main processing loop (tight spin)
    while (!ShutdownManager::instance().is_shutdown_requested()) {
        SageMessage msg;
        if (g_ade_to_rme_buffer.try_pop(msg)) {
            if (msg.msg_type == MessageType::SIGNAL) {
                process_signal(msg);
            } else if (msg.msg_type == MessageType::HEARTBEAT) {
                g_rme_to_poe_buffer.try_push(msg);
            }
        } else {
            cpu::pause();
        }
    }
    
    std::cout << "[RME] Shutting down..." << std::endl;
    hb_thread.join();
    
    // Final stats
    std::cout << "[RME] Final: approved=" << g_orders_approved.load()
              << " rejected=" << g_orders_rejected.load()
              << std::endl;
    
    return 0;
}
