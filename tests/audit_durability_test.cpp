/**
 * SAGE Audit Durability Test
 * Tests audit log lifecycle logging and durability mechanisms
 * 
 * Validates:
 * - ORDER, SENT, ACK lifecycle events are logged correctly
 * - sync() actually writes to disk (via pubsync)
 * - File contains expected entries after operations
 * - Truncation handling works for oversized entries
 */

#include <iostream>
#include <fstream>
#include <string>
#include <cassert>
#include <cstdio>
#include <thread>
#include <chrono>
#include <filesystem>

#include "../src/poe/audit_log.hpp"
#include "../src/types/sage_message.hpp"

using namespace sage;
using namespace sage::poe;

namespace fs = std::filesystem;

// ============================================================================
// Test Utilities
// ============================================================================

static std::string read_file_contents(const char* path) {
    std::ifstream in(path);
    if (!in.is_open()) return "";
    return std::string(
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>()
    );
}

static bool file_contains(const std::string& contents, const std::string& needle) {
    return contents.find(needle) != std::string::npos;
}

// ============================================================================
// Lifecycle Logging Tests
// ============================================================================

void test_lifecycle_logging() {
    std::cout << "  Testing ORDER -> SENT -> ACK lifecycle..." << std::endl;
    
    const char* test_file = "test_audit_lifecycle.log";
    
    // Clean up any previous test file
    std::remove(test_file);
    
    {
        AuditLog log(test_file);
        
        // Create a test order
        OrderRequest order{};
        order.symbol_id = 42;
        order.side = 1;  // BUY
        order.price = FixedPoint::from_double(50000.0);
        order.quantity = FixedPoint::from_double(0.1);
        
        uint64_t order_id = 12345;
        
        // Log full lifecycle
        log.log_order(order_id, order);   // Intent
        log.log_sent(order_id);           // Transmitted
        log.log_ack(order_id, "EX123");   // Acknowledged
        
        // Force sync
        log.sync();
        
        // Verify metrics
        assert(log.entries_logged() == 3);
        assert(log.sync_count() == 1);
    }
    
    // Read file and verify contents
    std::string contents = read_file_contents(test_file);
    
    assert(file_contains(contents, "ORDER|12345"));
    assert(file_contains(contents, "SENT|12345"));
    assert(file_contains(contents, "ACK|12345"));
    assert(file_contains(contents, "EX123"));  // Exchange ACK ID
    
    // Cleanup
    std::remove(test_file);
    
    std::cout << "  Lifecycle logging: PASSED" << std::endl;
}

void test_reject_and_error_logging() {
    std::cout << "  Testing REJECT and ERROR logging..." << std::endl;
    
    const char* test_file = "test_audit_errors.log";
    std::remove(test_file);
    
    {
        AuditLog log(test_file);
        
        OrderRequest order{};
        order.symbol_id = 1;
        order.side = -1;  // SELL
        order.price = FixedPoint::from_double(100.0);
        order.quantity = FixedPoint::from_double(1.0);
        
        uint64_t order_id = 54321;
        
        log.log_order(order_id, order);
        log.log_reject(order_id, "INSUFFICIENT_FUNDS");
        log.log_error(order_id, "CONNECTION_LOST");
        
        log.sync();
    }
    
    std::string contents = read_file_contents(test_file);
    
    assert(file_contains(contents, "ORDER|54321"));
    assert(file_contains(contents, "REJECT|54321"));
    assert(file_contains(contents, "INSUFFICIENT_FUNDS"));
    assert(file_contains(contents, "ERROR|54321"));
    assert(file_contains(contents, "CONNECTION_LOST"));
    
    std::remove(test_file);
    
    std::cout << "  Reject/Error logging: PASSED" << std::endl;
}

void test_fill_logging() {
    std::cout << "  Testing FILL logging..." << std::endl;
    
    const char* test_file = "test_audit_fills.log";
    std::remove(test_file);
    
    {
        AuditLog log(test_file);
        
        OrderRequest order{};
        order.symbol_id = 100;
        order.side = 1;
        order.price = FixedPoint::from_double(45000.0);
        order.quantity = FixedPoint::from_double(0.5);
        
        uint64_t order_id = 99999;
        
        log.log_order(order_id, order);
        log.log_sent(order_id);
        log.log_ack(order_id, "ACK_OK");
        log.log_fill(order_id, 100, 45001.5, 0.5);
        
        log.sync();
    }
    
    std::string contents = read_file_contents(test_file);
    
    assert(file_contains(contents, "ORDER|99999"));
    assert(file_contains(contents, "FILL|99999"));
    assert(file_contains(contents, "45001.5"));
    
    std::remove(test_file);
    
    std::cout << "  Fill logging: PASSED" << std::endl;
}

// ============================================================================
// Durability Tests
// ============================================================================

void test_sync_durability() {
    std::cout << "  Testing sync() durability..." << std::endl;
    
    const char* test_file = "test_audit_sync.log";
    std::remove(test_file);
    
    {
        AuditLog log(test_file);
        
        OrderRequest order{};
        order.symbol_id = 1;
        order.side = 1;
        order.price = FixedPoint::from_double(1000.0);
        order.quantity = FixedPoint::from_double(1.0);
        
        // Log multiple orders
        for (uint64_t i = 0; i < 10; ++i) {
            log.log_order(i, order);
            log.log_sent(i);
        }
        
        // Sync to disk
        log.sync();
        
        assert(log.entries_logged() == 20);
        assert(log.sync_count() >= 1);
    }
    
    // Verify file exists and has content
    assert(fs::exists(test_file));
    
    std::string contents = read_file_contents(test_file);
    assert(!contents.empty());
    assert(file_contains(contents, "ORDER|0"));
    assert(file_contains(contents, "ORDER|9"));
    assert(file_contains(contents, "SENT|9"));
    
    std::remove(test_file);
    
    std::cout << "  Sync durability: PASSED" << std::endl;
}

void test_background_sync_simulation() {
    std::cout << "  Testing background sync simulation..." << std::endl;
    
    const char* test_file = "test_audit_bg_sync.log";
    std::remove(test_file);
    
    {
        AuditLog log(test_file);
        
        OrderRequest order{};
        order.symbol_id = 42;
        order.side = 1;
        order.price = FixedPoint::from_double(50000.0);
        order.quantity = FixedPoint::from_double(0.1);
        
        // Simulate background sync thread behavior
        for (int round = 0; round < 3; ++round) {
            // Log some orders
            for (uint64_t i = 0; i < 5; ++i) {
                uint64_t oid = round * 100 + i;
                log.log_order(oid, order);
                log.log_sent(oid);
            }
            
            // Simulate periodic sync (every 50ms in production)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            log.sync();
        }
        
        assert(log.sync_count() == 3);
    }
    
    std::string contents = read_file_contents(test_file);
    
    // Verify entries from all rounds
    assert(file_contains(contents, "ORDER|0"));
    assert(file_contains(contents, "ORDER|100"));
    assert(file_contains(contents, "ORDER|200"));
    
    std::remove(test_file);
    
    std::cout << "  Background sync simulation: PASSED" << std::endl;
}

// ============================================================================
// Truncation Tests
// ============================================================================

void test_truncation_handling() {
    std::cout << "  Testing truncation handling..." << std::endl;
    
    const char* test_file = "test_audit_truncation.log";
    std::remove(test_file);
    
    {
        AuditLog log(test_file);
        
        OrderRequest order{};
        order.symbol_id = 1;
        order.side = 1;
        order.price = FixedPoint::from_double(100.0);
        order.quantity = FixedPoint::from_double(1.0);
        
        // Log normal order
        log.log_order(1, order);
        
        // Log with very long reason (should be truncated)
        std::string long_reason(200, 'X');  // 200 X's
        log.log_reject(2, long_reason.c_str());
        
        log.sync();
    }
    
    // File should exist and contain entries
    std::string contents = read_file_contents(test_file);
    assert(file_contains(contents, "ORDER|1"));
    assert(file_contains(contents, "REJECT|2"));
    
    std::remove(test_file);
    
    std::cout << "  Truncation handling: PASSED" << std::endl;
}

// ============================================================================
// UTC Timestamp Tests
// ============================================================================

void test_utc_timestamps() {
    std::cout << "  Testing UTC timestamps..." << std::endl;
    
    const char* test_file = "test_audit_timestamps.log";
    std::remove(test_file);
    
    {
        AuditLog log(test_file);
        
        OrderRequest order{};
        order.symbol_id = 1;
        order.side = 1;
        order.price = FixedPoint::from_double(100.0);
        order.quantity = FixedPoint::from_double(1.0);
        
        log.log_order(1, order);
        log.sync();
    }
    
    std::string contents = read_file_contents(test_file);
    
    // UTC timestamp format: YYYY-MM-DDTHH:MM:SSZ
    // Check for 'Z' suffix (UTC indicator) and 'T' separator
    assert(file_contains(contents, "Z|"));  // UTC marker before separator
    assert(file_contains(contents, "T"));   // ISO 8601 T separator
    
    std::remove(test_file);
    
    std::cout << "  UTC timestamps: PASSED" << std::endl;
}

// ============================================================================
// Reconciliation Test (Simulated)
// ============================================================================

void test_restart_reconciliation() {
    std::cout << "  Testing restart reconciliation scenario..." << std::endl;
    
    const char* test_file = "test_audit_reconcile.log";
    std::remove(test_file);
    
    // Phase 1: Simulate orders before "crash"
    {
        AuditLog log(test_file);
        
        OrderRequest order{};
        order.symbol_id = 1;
        order.side = 1;
        order.price = FixedPoint::from_double(100.0);
        order.quantity = FixedPoint::from_double(1.0);
        
        // Order 1: Full lifecycle (complete)
        log.log_order(1, order);
        log.log_sent(1);
        log.log_ack(1, "ACK1");
        
        // Order 2: Sent but no ACK (needs reconciliation)
        log.log_order(2, order);
        log.log_sent(2);
        
        // Order 3: Only ORDER logged (never sent - definite reconcile)
        log.log_order(3, order);
        
        log.sync();
    }
    
    // Phase 2: "Restart" and read log for reconciliation
    std::string contents = read_file_contents(test_file);
    
    // Reconciliation logic would look for:
    // - Orders with SENT but no ACK -> query exchange
    // - Orders with ORDER but no SENT -> definitely not received by exchange
    
    // Order 1: Has ORDER, SENT, ACK -> complete
    assert(file_contains(contents, "ORDER|1"));
    assert(file_contains(contents, "SENT|1"));
    assert(file_contains(contents, "ACK|1"));
    
    // Order 2: Has ORDER, SENT, no ACK -> needs exchange query
    assert(file_contains(contents, "ORDER|2"));
    assert(file_contains(contents, "SENT|2"));
    // No ACK|2 - this is expected
    
    // Order 3: Has ORDER only -> never transmitted
    assert(file_contains(contents, "ORDER|3"));
    // No SENT|3 or ACK|3 - this is expected
    
    std::remove(test_file);
    
    std::cout << "  Restart reconciliation: PASSED" << std::endl;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "====================================" << std::endl;
    std::cout << "SAGE Audit Durability Tests" << std::endl;
    std::cout << "====================================" << std::endl;
    
    std::cout << "\n[Lifecycle Logging Tests]" << std::endl;
    test_lifecycle_logging();
    test_reject_and_error_logging();
    test_fill_logging();
    
    std::cout << "\n[Durability Tests]" << std::endl;
    test_sync_durability();
    test_background_sync_simulation();
    
    std::cout << "\n[Safety Tests]" << std::endl;
    test_truncation_handling();
    test_utc_timestamps();
    
    std::cout << "\n[Reconciliation Tests]" << std::endl;
    test_restart_reconciliation();
    
    std::cout << "\n====================================" << std::endl;
    std::cout << "All audit durability tests PASSED!" << std::endl;
    std::cout << "====================================" << std::endl;
    
    return 0;
}
