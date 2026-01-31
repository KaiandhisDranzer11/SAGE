#pragma once

#include <cstdint>
#include <cstddef>

namespace sage {

// ============================================================================
// CACHE AND MEMORY CONSTANTS
// ============================================================================

/// CPU cache line size (Intel/AMD x86-64)
constexpr size_t CACHE_LINE_SIZE = 64;

/// Page size (standard 4KB)
constexpr size_t PAGE_SIZE = 4096;

/// Huge page size (2MB)
constexpr size_t HUGE_PAGE_SIZE = 2 * 1024 * 1024;

// ============================================================================
// RING BUFFER CONSTANTS
// ============================================================================

/// Default ring buffer size (must be power of 2)
constexpr size_t DEFAULT_RING_BUFFER_SIZE = 1 << 20;  // 1M entries

/// Minimum ring buffer size
constexpr size_t MIN_RING_BUFFER_SIZE = 1 << 10;  // 1K entries

/// Maximum ring buffer size
constexpr size_t MAX_RING_BUFFER_SIZE = 1 << 24;  // 16M entries

// ============================================================================
// FIXED POINT CONSTANTS
// ============================================================================

/// Fixed-point scale factor (10^8 for 8 decimal places)
constexpr int64_t PRICE_SCALE = 100'000'000LL;

/// Maximum representable price (avoid overflow in multiplication)
constexpr int64_t MAX_PRICE_SCALED = 9'223'372'036LL * PRICE_SCALE;

// ============================================================================
// TIME CONSTANTS
// ============================================================================

/// Nanoseconds per second
constexpr uint64_t NANOS_PER_SEC = 1'000'000'000ULL;

/// Nanoseconds per millisecond
constexpr uint64_t NANOS_PER_MS = 1'000'000ULL;

/// Nanoseconds per microsecond
constexpr uint64_t NANOS_PER_US = 1'000ULL;

// ============================================================================
// SYSTEM LIMITS
// ============================================================================

/// Maximum symbol name length
constexpr size_t MAX_SYMBOL_LEN = 16;

/// Maximum error message length
constexpr size_t MAX_ERROR_MSG_LEN = 32;

/// Maximum consumers per ring buffer
constexpr size_t MAX_CONSUMERS = 32;

/// Maximum exchanges supported
constexpr size_t MAX_EXCHANGES = 8;

// ============================================================================
// CPU CORE ASSIGNMENTS
// ============================================================================

/// Core 0: OS, SSH, background tasks
constexpr int CORE_OS = 0;

/// Core 1: CAL (Network I/O)
constexpr int CORE_CAL = 1;

/// Core 2: ADE (Analytics)
constexpr int CORE_ADE = 2;

/// Core 3: MIND (ML Inference)
constexpr int CORE_MIND = 3;

/// Core 4: RME (Risk)
constexpr int CORE_RME = 4;

/// Core 5: POE (Execution)
constexpr int CORE_POE = 5;

// ============================================================================
// MAGIC NUMBERS FOR VALIDATION
// ============================================================================

/// Ring buffer magic (ASCII: "SAGEBUF0")
constexpr uint64_t MAGIC_RING_BUFFER = 0x5341474542554630ULL;

/// Message magic (ASCII: "SAGEMSG0")
constexpr uint64_t MAGIC_MESSAGE = 0x534147454D534730ULL;

/// Shared memory magic (ASCII: "SAGESHM0")
constexpr uint64_t MAGIC_SHM = 0x5341474553484D30ULL;

} // namespace sage
