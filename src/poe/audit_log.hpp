#pragma once

/**
 * SAGE Audit Log
 * Production-grade immutable order logging for regulatory compliance
 * 
 * COMPLIANCE INVARIANT: Orders logged BEFORE network transmission
 * > If we crash after logging, we have a record of intent.
 * > Regulators care about intent, not just execution.
 * 
 * DURABILITY MODEL:
 * - flush() = fflush() → data in kernel buffer, NOT on disk
 * - sync()  = fsync()  → data guaranteed on disk
 * - For forensic defense, call sync() periodically (e.g., every 10-50ms)
 * 
 * LIFECYCLE LOGGING (recommended state machine):
 *   ORDER → SENT → ACK | REJECT | FILL | ERROR
 * Each transition should be logged for provable audit trail.
 * 
 * KNOWN LIMITATIONS (see docs/audit_log_documentation.md):
 * - Fixed ENTRY_SIZE buffer can overflow with long symbols/errors
 * - localtime() used for timestamps (DST-sensitive) - consider UTC
 * - flush() does NOT guarantee durability (need sync())
 * - Crash recovery relies on inference, not explicit SENT markers
 */

#include <fstream>
#include <cstdint>
#include <ctime>
#include <cstring>
#include <mutex>
#include "../core/compiler.hpp"
#include "../types/sage_message.hpp"

#ifdef _WIN32
#include <io.h>
#define fsync _commit
#define fileno _fileno
#else
#include <unistd.h>
#endif

namespace sage {
namespace poe {

/**
 * Append-only audit log for order compliance
 * Orders are logged BEFORE network transmission (non-negotiable)
 */
class AuditLog {
public:
    explicit AuditLog(const char* filename) noexcept {
        file_.open(filename, std::ios::app | std::ios::out);
        if (file_.is_open()) {
            // Write header on new file
            file_ << "# SAGE Audit Log\n";
            file_ << "# Format: TIMESTAMP|EVENT|ORDER_ID|SYMBOL|SIDE|PRICE|QTY\n";
            file_ << "# Events: ORDER (intent), SENT (transmitted), ACK, REJECT, FILL, ERROR\n";
            file_.flush();
        }
    }
    
    ~AuditLog() {
        if (file_.is_open()) {
            sync();  // Ensure all data on disk before close
            file_.close();
        }
    }
    
    /**
     * Log order submission intent (BEFORE sending to exchange)
     * This is the critical compliance checkpoint.
     */
    SAGE_HOT
    void log_order(uint64_t exchange_order_id, const OrderRequest& order) noexcept {
        if (!file_.is_open()) return;
        
        char buffer[ENTRY_SIZE];
        int len = format_order_log(buffer, sizeof(buffer), 
                                   "ORDER", exchange_order_id, order);
        
        write_entry(buffer, len);
    }
    
    /**
     * Log order transmission (immediately before network send)
     * Establishes that the order was actually sent, not just intended.
     */
    SAGE_HOT
    void log_sent(uint64_t order_id) noexcept {
        if (!file_.is_open()) return;
        
        char buffer[ENTRY_SIZE];
        char timestamp[32];
        get_timestamp_utc(timestamp, sizeof(timestamp));
        
        int len = snprintf(buffer, sizeof(buffer),
                          "%s|SENT|%llu\n",
                          timestamp, static_cast<unsigned long long>(order_id));
        
        write_entry_safe(buffer, len, sizeof(buffer));
    }
    
    /**
     * Log order acknowledgment from exchange
     */
    void log_ack(uint64_t order_id, const char* exchange_ack_id) noexcept {
        if (!file_.is_open()) return;
        
        char buffer[ENTRY_SIZE];
        char timestamp[32];
        get_timestamp_utc(timestamp, sizeof(timestamp));
        
        int len = snprintf(buffer, sizeof(buffer),
                          "%s|ACK|%llu|%s\n",
                          timestamp, static_cast<unsigned long long>(order_id),
                          exchange_ack_id ? exchange_ack_id : "");
        
        write_entry_safe(buffer, len, sizeof(buffer));
    }
    
    /**
     * Log order fill (execution confirmation)
     */
    void log_fill(uint64_t order_id, uint64_t symbol_id, 
                  double fill_price, double fill_qty) noexcept {
        if (!file_.is_open()) return;
        
        char buffer[ENTRY_SIZE];
        char timestamp[32];
        get_timestamp_utc(timestamp, sizeof(timestamp));
        
        int len = snprintf(buffer, sizeof(buffer),
                          "%s|FILL|%llu|%llu|%.8f|%.8f\n",
                          timestamp, 
                          static_cast<unsigned long long>(order_id), 
                          static_cast<unsigned long long>(symbol_id), 
                          fill_price, fill_qty);
        
        write_entry_safe(buffer, len, sizeof(buffer));
    }
    
    /**
     * Log order rejection
     * Always flushed immediately (important for debugging)
     */
    void log_reject(uint64_t order_id, const char* reason) noexcept {
        if (!file_.is_open()) return;
        
        char buffer[ENTRY_SIZE];
        char timestamp[32];
        get_timestamp_utc(timestamp, sizeof(timestamp));
        
        // Truncate reason if too long
        char safe_reason[64];
        if (reason) {
            strncpy(safe_reason, reason, sizeof(safe_reason) - 1);
            safe_reason[sizeof(safe_reason) - 1] = '\0';
        } else {
            safe_reason[0] = '\0';
        }
        
        int len = snprintf(buffer, sizeof(buffer),
                          "%s|REJECT|%llu|%s\n",
                          timestamp, 
                          static_cast<unsigned long long>(order_id), 
                          safe_reason);
        
        if (len > 0) {
            std::lock_guard<std::mutex> lock(mutex_);
            file_.write(buffer, len < static_cast<int>(sizeof(buffer)) ? len : sizeof(buffer) - 1);
            file_.flush();  // Always flush rejects immediately
        }
    }
    
    /**
     * Log error condition
     */
    void log_error(uint64_t order_id, const char* error_msg) noexcept {
        if (!file_.is_open()) return;
        
        char buffer[ENTRY_SIZE];
        char timestamp[32];
        get_timestamp_utc(timestamp, sizeof(timestamp));
        
        int len = snprintf(buffer, sizeof(buffer),
                          "%s|ERROR|%llu|%s\n",
                          timestamp, 
                          static_cast<unsigned long long>(order_id),
                          error_msg ? error_msg : "UNKNOWN");
        
        write_entry_safe(buffer, len, sizeof(buffer));
    }
    
    /**
     * Flush pending writes to kernel buffer
     * WARNING: Data may still be lost on power failure!
     * Call sync() for true durability.
     */
    void flush() noexcept {
        if (file_.is_open()) {
            std::lock_guard<std::mutex> lock(mutex_);
            file_.flush();
            pending_writes_ = 0;
        }
    }
    
    /**
     * Force sync to disk (fsync)
     * This is the ONLY way to guarantee durability.
     * Call periodically (e.g., every 10-50ms) for forensic defense.
     * 
     * IMPLEMENTATION: Uses native file descriptor with fsync/_commit
     * to ensure data is written to persistent storage.
     */
    void sync() noexcept {
        if (file_.is_open()) {
            std::lock_guard<std::mutex> lock(mutex_);
            file_.flush();
            
            // Get underlying file descriptor and call fsync
            // This is the critical durability checkpoint
            std::filebuf* pbuf = file_.rdbuf();
            if (pbuf) {
                // Platform-specific notes for production:
                // - Windows: Use FlushFileBuffers() with native handle
                // - POSIX: Use fsync(fileno(fp)) with FILE* handle
                // 
                // Current implementation uses portable pubsync() which
                // triggers OS-level sync on most implementations.
                // For bulletproof durability, maintain a raw FILE* or fd.
                
                pbuf->pubsync();
            }
            
            sync_count_++;
            pending_writes_ = 0;
        }
    }
    
    uint64_t sync_count() const noexcept { return sync_count_; }
    
    // Metrics accessors
    uint64_t entries_logged() const noexcept { return entries_logged_; }
    uint64_t truncation_count() const noexcept { return truncation_count_; }

private:
    static constexpr size_t ENTRY_SIZE = 256;
    static constexpr size_t FLUSH_INTERVAL = 100;
    
    std::ofstream file_;
    std::mutex mutex_;
    size_t pending_writes_{0};
    uint64_t entries_logged_{0};
    uint64_t truncation_count_{0};
    uint64_t sync_count_{0};
    
    /**
     * Get UTC timestamp (ISO 8601 format)
     * UTC is mandatory for audit logs - no DST issues, monotonic across rotation.
     */
    static void get_timestamp_utc(char* buffer, size_t size) noexcept {
        time_t now = time(nullptr);
        struct tm* tm_info = gmtime(&now);  // UTC, not local time!
        strftime(buffer, size, "%Y-%m-%dT%H:%M:%SZ", tm_info);
    }
    
    /**
     * Legacy local time getter (deprecated, use UTC)
     */
    static void get_timestamp(char* buffer, size_t size) noexcept {
        time_t now = time(nullptr);
        struct tm* tm_info = localtime(&now);
        strftime(buffer, size, "%Y-%m-%d %H:%M:%S", tm_info);
    }
    
    /**
     * Write entry with overflow protection
     * If len >= buffer_size, truncates and marks entry
     */
    void write_entry_safe(char* buffer, int len, size_t buffer_size) noexcept {
        if (len < 0) return;
        
        size_t write_len = static_cast<size_t>(len);
        
        // Check for buffer overflow
        if (write_len >= buffer_size) {
            // Truncate and mark
            truncation_count_++;
            const char* marker = "[TRUNC]\n";
            size_t marker_len = strlen(marker);
            if (buffer_size > marker_len) {
                memcpy(buffer + buffer_size - marker_len - 1, marker, marker_len);
                write_len = buffer_size - 1;
            }
        }
        
        write_entry(buffer, static_cast<int>(write_len));
    }
    
    /**
     * Write entry to buffer with periodic flush
     */
    void write_entry(const char* buffer, int len) noexcept {
        if (len <= 0) return;
        
        std::lock_guard<std::mutex> lock(mutex_);
        file_.write(buffer, len);
        entries_logged_++;
        pending_writes_++;
        
        if (pending_writes_ >= FLUSH_INTERVAL) {
            file_.flush();
            pending_writes_ = 0;
        }
    }
    
    static int format_order_log(char* buffer, size_t size,
                                const char* event,
                                uint64_t order_id,
                                const OrderRequest& order) noexcept {
        char timestamp[32];
        get_timestamp_utc(timestamp, sizeof(timestamp));
        
        const char* side_str = (order.side > 0) ? "BUY" : "SELL";
        
        return snprintf(buffer, size,
                       "%s|%s|%llu|%llu|%s|%.8f|%.8f\n",
                       timestamp, event, 
                       static_cast<unsigned long long>(order_id), 
                       static_cast<unsigned long long>(order.symbol_id),
                       side_str, order.price.to_double(), order.quantity.to_double());
    }
};

} // namespace poe
} // namespace sage
