/**
 * SAGE POE - Order Execution Engine
 * Production-grade order submission with audit trail
 * 
 * COMPLIANCE REQUIREMENTS:
 * - Every order MUST be logged BEFORE network transmission (audit_log.log_order)
 * - Transmission SHOULD be logged immediately after send (audit_log.log_sent)
 * - ACK/REJECT/FILL logged on receipt from exchange
 * 
 * DURABILITY MODEL:
 * - Audit log uses buffered writes (50ns per entry)
 * - Background thread calls sync() every FSYNC_INTERVAL_MS for durability
 * - On crash, recent entries may be in kernel buffer but not on disk
 * - For forensic defense, reduce FSYNC_INTERVAL_MS (costs latency)
 * 
 * See docs/audit_log_documentation.md for full compliance details.
 */

#include <iostream>
#include <thread>
#include <atomic>
#include <fstream>
#include <cstring>
#include <chrono>

#include "../core/compiler.hpp"
#include "../core/constants.hpp"
#include "../core/timing.hpp"
#include "../core/cpu_affinity.hpp"
#include "../core/shutdown.hpp"
#include "../infra/ring_buffer.hpp"
#include "../types/sage_message.hpp"
#include "order_id_gen.hpp"
#include "audit_log.hpp"
#include "fix_encoder.hpp"

using namespace sage;

// ============================================================================
// Configuration
// ============================================================================

constexpr size_t AUDIT_BUFFER_SIZE = 4096;
constexpr size_t FIX_BUFFER_SIZE = 512;
constexpr int FSYNC_INTERVAL_MS = 50;  // Fsync every 50ms for durability

// ============================================================================
// Global State
// ============================================================================

// Ring buffer
static RingBuffer<SageMessage, 65536> g_rme_to_poe_buffer;

// Order ID generator
static poe::OrderIDGenerator g_order_id_gen;

// Audit log
static poe::AuditLog g_audit_log("sage_audit.log");

// Pre-allocated FIX message buffer
static thread_local char g_fix_buffer[FIX_BUFFER_SIZE];

// Metrics
static std::atomic<uint64_t> g_orders_sent{0};
static std::atomic<uint64_t> g_orders_failed{0};
static std::atomic<uint64_t> g_bytes_sent{0};
static std::atomic<uint64_t> g_total_latency_ns{0};

// TSC calibrator
static timing::TSCCalibrator g_tsc_calibrator;

// ============================================================================
// Mock Network Send (Replace with real TCP/FIX in production)
// ============================================================================

SAGE_HOT
static bool send_to_exchange(const char* data, size_t len) noexcept {
    // In production: send(socket_fd, data, len, MSG_NOSIGNAL)
    // For now, just track bytes
    g_bytes_sent.fetch_add(len, std::memory_order_relaxed);
    return true;
}

// ============================================================================
// Hot Path Processing
// ============================================================================

/**
 * Process order with full lifecycle logging
 * 
 * Lifecycle: ORDER (intent) → SENT (transmitted) → ACK/REJECT/FILL
 */
SAGE_HOT SAGE_FLATTEN
static void process_order(const SageMessage& msg) noexcept {
    const uint64_t start_tsc = timing::rdtsc();
    
    const auto& order = msg.payload.order;
    
    // Generate unique order ID
    uint64_t exchange_order_id = g_order_id_gen.generate();
    
    // CRITICAL: Log intent BEFORE network transmission
    // This ensures we have a record even if send fails or crashes
    g_audit_log.log_order(exchange_order_id, order);
    
    // Encode FIX message into pre-allocated buffer
    size_t fix_len = poe::FIXEncoder::encode_new_order_fast(
        g_fix_buffer,
        FIX_BUFFER_SIZE,
        exchange_order_id,
        order.symbol_id,
        order.side,
        order.price.to_double(),
        order.quantity.to_double()
    );
    
    // Send to exchange
    bool send_success = send_to_exchange(g_fix_buffer, fix_len);
    
    // Log transmission event (marks order as actually sent)
    // This distinguishes "intended to send" from "actually transmitted"
    if (send_success) [[likely]] {
        g_audit_log.log_sent(exchange_order_id);
        g_orders_sent.fetch_add(1, std::memory_order_relaxed);
    } else {
        g_audit_log.log_error(exchange_order_id, "SEND_FAILED");
        g_orders_failed.fetch_add(1, std::memory_order_relaxed);
    }
    
    // Track latency
    const uint64_t latency_tsc = timing::rdtsc() - start_tsc;
    g_total_latency_ns.fetch_add(
        g_tsc_calibrator.tsc_to_ns(latency_tsc),
        std::memory_order_relaxed
    );
}


// ============================================================================
// Background Fsync Thread (Audit Durability)
// ============================================================================

/**
 * Background thread that periodically calls fsync on audit log
 * 
 * This provides forensic durability without blocking the hot path.
 * Trade-off: 50ms window where data may be in kernel buffer only.
 * Reduce FSYNC_INTERVAL_MS for tighter durability (costs latency indirectly).
 */
static void fsync_thread() {
    cpu::pin_to_core(CORE_OS);  // Low priority core
    
    while (!ShutdownManager::instance().is_shutdown_requested()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(FSYNC_INTERVAL_MS));
        
        // Force sync to disk - this is the durability checkpoint
        g_audit_log.sync();
    }
}

// ============================================================================
// Heartbeat Thread
// ============================================================================

static void heartbeat_thread() {
    cpu::pin_to_core(CORE_OS);
    
    while (!ShutdownManager::instance().is_shutdown_requested()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        uint64_t sent = g_orders_sent.load();
        uint64_t failed = g_orders_failed.load();
        uint64_t bytes = g_bytes_sent.load();
        uint64_t total_latency = g_total_latency_ns.load();
        
        double avg_latency_ns = (sent > 0) ? 
            static_cast<double>(total_latency) / sent : 0.0;
        
        std::cout << "[POE] Stats: sent=" << sent
                  << " failed=" << failed
                  << " bytes=" << bytes
                  << " avg_latency=" << avg_latency_ns << "ns"
                  << " queue=" << g_rme_to_poe_buffer.size_approx()
                  << " audit_entries=" << g_audit_log.entries_logged()
                  << std::endl;
        
        // Flush for visibility (sync thread handles durability)
        g_audit_log.flush();

    }
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "[POE] Starting Order Execution Engine..." << std::endl;
    std::cout << "[POE] Fsync interval: " << FSYNC_INTERVAL_MS << "ms" << std::endl;
    
    // Pin to designated core
    if (cpu::pin_to_core(CORE_POE) == 0) {
        std::cout << "[POE] Pinned to core " << CORE_POE << std::endl;
    }
    
    // Try real-time priority
    if (cpu::set_realtime_priority(70) == 0) {
        std::cout << "[POE] Real-time priority set (70)" << std::endl;
    }
    
    ShutdownManager::instance().install_signal_handlers();
    
    // Register shutdown handler to sync audit log (durability on shutdown)
    ShutdownManager::instance().register_handler([]() {
        std::cout << "[POE] Syncing audit log to disk..." << std::endl;
        g_audit_log.sync();  // sync(), not just flush()
    });
    
    // Start background fsync thread (audit durability)
    std::thread sync_thread(fsync_thread);
    
    // Start heartbeat thread
    std::thread hb_thread(heartbeat_thread);
    
    std::cout << "[POE] Entering main loop..." << std::endl;
    
    // Main processing loop
    while (!ShutdownManager::instance().is_shutdown_requested()) {
        SageMessage msg;
        if (g_rme_to_poe_buffer.try_pop(msg)) {
            if (msg.msg_type == MessageType::ORDER_REQUEST) {
                process_order(msg);
            } else if (msg.msg_type == MessageType::SHUTDOWN) {
                std::cout << "[POE] Received shutdown message" << std::endl;
                break;
            }
        } else {
            cpu::pause();
        }
    }
    
    std::cout << "[POE] Shutting down..." << std::endl;
    
    // Wait for background threads
    sync_thread.join();
    hb_thread.join();
    
    // Final sync to ensure all audit data on disk
    g_audit_log.sync();
    
    // Final stats
    std::cout << "[POE] Final: sent=" << g_orders_sent.load()
              << " failed=" << g_orders_failed.load()
              << " bytes=" << g_bytes_sent.load()
              << " audit_entries=" << g_audit_log.entries_logged()
              << std::endl;
    
    return 0;
}

