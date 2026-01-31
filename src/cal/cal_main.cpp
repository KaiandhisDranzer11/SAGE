/**
 * SAGE CAL - Connector Abstraction Layer
 * Production-grade market data ingestion
 */

#include <iostream>
#include <thread>
#include <atomic>

#include "../core/compiler.hpp"
#include "../core/constants.hpp"
#include "../core/timing.hpp"
#include "../core/cpu_affinity.hpp"
#include "../core/shutdown.hpp"
#include "../infra/ring_buffer.hpp"
#include "../types/sage_message.hpp"
#include "websocket_client.hpp"
#include "json_parser.hpp"
#include "validator.hpp"

using namespace sage;

// ============================================================================
// Global State
// ============================================================================

// Ring buffer for CAL -> ADE communication
// In production: mmap'd shared memory
static RingBuffer<SageMessage, 65536> g_cal_to_ade_buffer;

// Metrics
static std::atomic<uint64_t> g_messages_received{0};
static std::atomic<uint64_t> g_messages_dropped{0};
static std::atomic<uint64_t> g_validation_errors{0};

// Sequence counter (not atomic - single producer)
static uint64_t g_sequence = 0;

// TSC calibrator (initialized once at startup)
static timing::TSCCalibrator g_tsc_calibrator;

// ============================================================================
// Message Processing (Hot Path)
// ============================================================================

SAGE_HOT SAGE_FLATTEN
static void process_message(const char* data, size_t len) noexcept {
    static cal::JsonParser parser;
    
    // Get timestamp immediately (lowest latency)
    const uint64_t timestamp = timing::rdtscp();
    
    // Parse JSON
    auto result = parser.parse_trade(data, len);
    if (!result) [[unlikely]] {
        return;
    }
    
    // Validate
    if (!cal::Validator::validate_market_data(*result)) [[unlikely]] {
        g_validation_errors.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    
    // Create message
    SageMessage msg;
    msg.timestamp_ns = g_tsc_calibrator.tsc_to_ns(timestamp);
    msg.sequence_id = ++g_sequence;
    msg.msg_type = MessageType::MARKET_DATA;
    msg.payload.market_data = *result;
    
    // Push to ring buffer
    if (!g_cal_to_ade_buffer.try_push(msg)) [[unlikely]] {
        g_messages_dropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    
    g_messages_received.fetch_add(1, std::memory_order_relaxed);
}

// ============================================================================
// Heartbeat Thread
// ============================================================================

static void heartbeat_thread() {
    // Pin to OS core (not critical path)
    cpu::pin_to_core(CORE_OS);
    
    uint64_t heartbeat_seq = 0;
    
    while (!ShutdownManager::instance().is_shutdown_requested()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // Create heartbeat message
        SageMessage hb = SageMessage::create_heartbeat(
            timing::get_monotonic_ns(),
            ++heartbeat_seq,
            1  // CAL component ID
        );
        
        // Best effort - don't block
        g_cal_to_ade_buffer.try_push(hb);
        
        // Log stats
        std::cout << "[CAL] Stats: received=" << g_messages_received.load()
                  << " dropped=" << g_messages_dropped.load()
                  << " errors=" << g_validation_errors.load()
                  << " queue=" << g_cal_to_ade_buffer.size_approx()
                  << std::endl;
    }
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "[CAL] Starting Connector Abstraction Layer..." << std::endl;
    std::cout << "[CAL] TSC calibration: " << g_tsc_calibrator.get_ticks_per_ns() 
              << " ticks/ns" << std::endl;
    
    // Pin to designated core
    if (cpu::pin_to_core(CORE_CAL) == 0) {
        std::cout << "[CAL] Pinned to core " << CORE_CAL << std::endl;
    }
    
    // Try to set real-time priority
    if (cpu::set_realtime_priority(50) == 0) {
        std::cout << "[CAL] Real-time priority set" << std::endl;
    }
    
    // Install signal handlers
    ShutdownManager::instance().install_signal_handlers();
    
    // Start heartbeat thread
    std::thread hb_thread(heartbeat_thread);
    
    // Initialize WebSocket client
    cal::WebSocketClient binance_ws(
        "wss://stream.binance.com:9443/ws/btcusdt@trade",
        process_message
    );
    
    binance_ws.start();
    std::cout << "[CAL] WebSocket client started" << std::endl;
    
    // Main loop - minimal work, just check shutdown
    while (!ShutdownManager::instance().is_shutdown_requested()) {
        // Low-power wait
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    std::cout << "[CAL] Shutting down..." << std::endl;
    
    // Cleanup
    binance_ws.stop();
    hb_thread.join();
    
    // Final stats
    std::cout << "[CAL] Final stats: received=" << g_messages_received.load()
              << " dropped=" << g_messages_dropped.load()
              << " errors=" << g_validation_errors.load()
              << std::endl;
    
    return 0;
}
