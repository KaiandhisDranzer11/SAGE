#pragma once

/**
 * SAGE Unified Message Protocol
 * 64-byte cache-aligned message for lock-free IPC
 */

#include <cstdint>
#include <cstring>
#include "fixed_point.hpp"
#include "../core/constants.hpp"
#include "../core/compiler.hpp"

namespace sage {

// ============================================================================
// Message Types
// ============================================================================

enum class MessageType : uint8_t {
    INVALID = 0,
    MARKET_DATA = 1,
    SIGNAL = 2,
    ORDER_REQUEST = 3,
    ORDER_ACK = 4,
    ORDER_FILL = 5,
    ORDER_CANCEL = 6,
    RISK_ALERT = 7,
    HEARTBEAT = 8,
    SHUTDOWN = 9
};

// ============================================================================
// Message Payloads
// ============================================================================

/**
 * Market data tick (trade or quote)
 * 32 bytes
 */
struct MarketData {
    FixedPoint price;      // 8 bytes
    FixedPoint quantity;   // 8 bytes
    uint64_t symbol_id;    // 8 bytes
    uint32_t flags;        // 4 bytes (bid=0x01, ask=0x02, trade=0x04)
    uint8_t exchange_id;   // 1 byte
    uint8_t reserved[3];   // 3 bytes padding
};
static_assert(sizeof(MarketData) == 32, "MarketData must be 32 bytes");

/**
 * Trading signal from MIND
 * 24 bytes
 */
struct Signal {
    uint64_t symbol_id;    // 8 bytes
    FixedPoint confidence; // 8 bytes (0.0 - 1.0 scaled)
    int8_t direction;      // 1 byte (+1 = buy, -1 = sell, 0 = neutral)
    uint8_t strategy_id;   // 1 byte
    uint8_t reserved[6];   // 6 bytes padding
};
static_assert(sizeof(Signal) == 24, "Signal must be 24 bytes");

/**
 * Order request from RME to POE
 * 40 bytes
 */
struct OrderRequest {
    uint64_t order_id;     // 8 bytes
    uint64_t symbol_id;    // 8 bytes
    FixedPoint price;      // 8 bytes
    FixedPoint quantity;   // 8 bytes
    int8_t side;           // 1 byte (+1 = buy, -1 = sell)
    uint8_t order_type;    // 1 byte (1=market, 2=limit, 3=ioc)
    uint8_t time_in_force; // 1 byte
    uint8_t reserved[5];   // 5 bytes padding
};
static_assert(sizeof(OrderRequest) == 40, "OrderRequest must be 40 bytes");

/**
 * Risk alert from RME
 * 40 bytes
 */
struct RiskAlert {
    uint64_t timestamp;    // 8 bytes
    int64_t exposure;      // 8 bytes
    int64_t daily_pnl;     // 8 bytes
    uint8_t alert_level;   // 1 byte (0=info, 1=warn, 2=critical)
    uint8_t reserved[15];  // 15 bytes padding
};
static_assert(sizeof(RiskAlert) == 40, "RiskAlert must be 40 bytes");

/**
 * Heartbeat for liveness detection
 * 16 bytes
 */
struct Heartbeat {
    uint64_t sequence;     // 8 bytes
    uint32_t component_id; // 4 bytes
    uint8_t status;        // 1 byte (0=ok, 1=degraded, 2=failing)
    uint8_t reserved[3];   // 3 bytes padding
};
static_assert(sizeof(Heartbeat) == 16, "Heartbeat must be 16 bytes");

// ============================================================================
// Unified Message Structure
// ============================================================================

/**
 * Main IPC message (exactly 64 bytes = 1 cache line)
 * 
 * Layout:
 *   [0-7]   timestamp_ns     (8 bytes)
 *   [8-15]  sequence_id      (8 bytes)
 *   [16]    msg_type         (1 byte)
 *   [17-23] reserved         (7 bytes)
 *   [24-63] payload          (40 bytes)
 */
struct SAGE_CACHE_ALIGNED SageMessage {
    // Header (24 bytes)
    uint64_t timestamp_ns;   // 8 bytes - Local receipt time
    uint64_t sequence_id;    // 8 bytes - Monotonic sequence
    MessageType msg_type;    // 1 byte
    uint8_t reserved[7];     // 7 bytes padding
    
    // Payload (40 bytes)
    union {
        MarketData market_data;
        Signal signal;
        OrderRequest order;
        RiskAlert risk_alert;
        Heartbeat heartbeat;
        uint8_t raw[40];
    } payload;
    
    // ========================================================================
    // Factory Methods
    // ========================================================================
    
    static SageMessage create_market_data(
        uint64_t timestamp,
        uint64_t seq,
        const MarketData& data
    ) noexcept {
        SageMessage msg{};
        msg.timestamp_ns = timestamp;
        msg.sequence_id = seq;
        msg.msg_type = MessageType::MARKET_DATA;
        msg.payload.market_data = data;
        return msg;
    }
    
    static SageMessage create_signal(
        uint64_t timestamp,
        uint64_t seq,
        const Signal& sig
    ) noexcept {
        SageMessage msg{};
        msg.timestamp_ns = timestamp;
        msg.sequence_id = seq;
        msg.msg_type = MessageType::SIGNAL;
        msg.payload.signal = sig;
        return msg;
    }
    
    static SageMessage create_heartbeat(
        uint64_t timestamp,
        uint64_t seq,
        uint32_t component_id
    ) noexcept {
        SageMessage msg{};
        msg.timestamp_ns = timestamp;
        msg.sequence_id = seq;
        msg.msg_type = MessageType::HEARTBEAT;
        msg.payload.heartbeat.sequence = seq;
        msg.payload.heartbeat.component_id = component_id;
        msg.payload.heartbeat.status = 0;
        return msg;
    }
    
    // ========================================================================
    // Validation
    // ========================================================================
    
    bool is_valid() const noexcept {
        return msg_type != MessageType::INVALID;
    }
};

// Compile-time verification
static_assert(sizeof(SageMessage) == 64, "SageMessage must be exactly 64 bytes (1 cache line)");
static_assert(alignof(SageMessage) == 64, "SageMessage must be 64-byte aligned");

} // namespace sage
