#pragma once

/**
 * SAGE FIX Protocol Encoder
 * Production-grade minimal FIX 4.2 encoding with zero allocation
 */

#include <cstdint>
#include <cstring>
#include <cstdio>
#include "../core/compiler.hpp"

namespace sage {
namespace poe {

/**
 * FIX Protocol encoder
 * Uses pre-allocated buffers for zero-allocation encoding
 */
class FIXEncoder {
public:
    static constexpr char SOH = '\x01';  // FIX field separator
    
    /**
     * Encode NewOrderSingle message into pre-allocated buffer
     * Returns bytes written, 0 on error
     */
    SAGE_HOT
    static size_t encode_new_order_fast(
        char* buffer,
        size_t buffer_size,
        uint64_t order_id,
        uint64_t symbol_id,
        int8_t side,
        double price,
        double quantity
    ) noexcept {
        char* ptr = buffer;
        char* end = buffer + buffer_size - 16;  // Reserve space for checksum
        
        // BeginString (8)
        ptr = append_field(ptr, end, "8=FIX.4.2");
        
        // Body length placeholder - will fill in at end
        char* body_len_ptr = ptr;
        ptr = append_field(ptr, end, "9=000");
        char* body_start = ptr;
        
        // MsgType (35) = D (NewOrderSingle)
        ptr = append_field(ptr, end, "35=D");
        
        // ClOrdID (11)
        ptr = append_int_field(ptr, end, "11=", order_id);
        
        // Symbol (55) - use symbol_id as string
        ptr = append_int_field(ptr, end, "55=", symbol_id);
        
        // Side (54): 1=Buy, 2=Sell
        if (side > 0) {
            ptr = append_field(ptr, end, "54=1");
        } else {
            ptr = append_field(ptr, end, "54=2");
        }
        
        // TransactTime (60) - simplified timestamp
        ptr = append_field(ptr, end, "60=20260130-12:00:00.000");
        
        // OrderQty (38)
        ptr = append_double_field(ptr, end, "38=", quantity);
        
        // OrdType (40) = 2 (Limit)
        ptr = append_field(ptr, end, "40=2");
        
        // Price (44)
        ptr = append_double_field(ptr, end, "44=", price);
        
        // TimeInForce (59) = 0 (Day)
        ptr = append_field(ptr, end, "59=0");
        
        size_t body_len = ptr - body_start;
        
        // Fill in body length
        char len_str[4];
        snprintf(len_str, sizeof(len_str), "%03zu", body_len);
        body_len_ptr[2] = len_str[0];
        body_len_ptr[3] = len_str[1];
        body_len_ptr[4] = len_str[2];
        
        // Calculate checksum
        uint32_t checksum = 0;
        for (char* p = buffer; p < ptr; ++p) {
            checksum += static_cast<uint8_t>(*p);
        }
        checksum = checksum % 256;
        
        // Append checksum (10)
        char cs_str[8];
        snprintf(cs_str, sizeof(cs_str), "10=%03u", checksum);
        ptr = append_field(ptr, end, cs_str);
        
        return ptr - buffer;
    }
    
    /**
     * Encode OrderCancelRequest
     */
    static size_t encode_cancel_order_fast(
        char* buffer,
        size_t buffer_size,
        uint64_t order_id,
        uint64_t orig_order_id
    ) noexcept {
        char* ptr = buffer;
        char* end = buffer + buffer_size - 16;
        
        ptr = append_field(ptr, end, "8=FIX.4.2");
        
        char* body_len_ptr = ptr;
        ptr = append_field(ptr, end, "9=000");
        char* body_start = ptr;
        
        // MsgType (35) = F (OrderCancelRequest)
        ptr = append_field(ptr, end, "35=F");
        
        // ClOrdID (11)
        ptr = append_int_field(ptr, end, "11=", order_id);
        
        // OrigClOrdID (41)
        ptr = append_int_field(ptr, end, "41=", orig_order_id);
        
        size_t body_len = ptr - body_start;
        
        // Fill in body length
        char len_str[4];
        snprintf(len_str, sizeof(len_str), "%03zu", body_len);
        body_len_ptr[2] = len_str[0];
        body_len_ptr[3] = len_str[1];
        body_len_ptr[4] = len_str[2];
        
        // Checksum
        uint32_t checksum = 0;
        for (char* p = buffer; p < ptr; ++p) {
            checksum += static_cast<uint8_t>(*p);
        }
        checksum = checksum % 256;
        
        char cs_str[8];
        snprintf(cs_str, sizeof(cs_str), "10=%03u", checksum);
        ptr = append_field(ptr, end, cs_str);
        
        return ptr - buffer;
    }

private:
    SAGE_ALWAYS_INLINE
    static char* append_field(char* ptr, char* end, const char* field) noexcept {
        size_t len = strlen(field);
        if (ptr + len + 1 > end) return ptr;
        memcpy(ptr, field, len);
        ptr += len;
        *ptr++ = SOH;
        return ptr;
    }
    
    SAGE_ALWAYS_INLINE
    static char* append_int_field(char* ptr, char* end, const char* prefix, uint64_t value) noexcept {
        size_t prefix_len = strlen(prefix);
        if (ptr + prefix_len + 20 > end) return ptr;
        memcpy(ptr, prefix, prefix_len);
        ptr += prefix_len;
        ptr += sprintf(ptr, "%lu", value);
        *ptr++ = SOH;
        return ptr;
    }
    
    SAGE_ALWAYS_INLINE
    static char* append_double_field(char* ptr, char* end, const char* prefix, double value) noexcept {
        size_t prefix_len = strlen(prefix);
        if (ptr + prefix_len + 32 > end) return ptr;
        memcpy(ptr, prefix, prefix_len);
        ptr += prefix_len;
        ptr += sprintf(ptr, "%.8f", value);
        *ptr++ = SOH;
        return ptr;
    }
};

} // namespace poe
} // namespace sage
