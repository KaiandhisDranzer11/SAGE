/**
 * SAGE ADE - Analytics & Decision Engine
 * Production-grade feature extraction and signal generation
 * 
 * Architecture Notes:
 * - Pre-allocated per-symbol state for O(1) lookup, no hot-path allocation
 * - Cache-aligned structures to prevent false sharing
 * - Fixed-point arithmetic for deterministic behavior
 * - Batch processing with prefetch for throughput
 * 
 * IMPROVEMENTS IMPLEMENTED:
 * - EWMA variance for faster regime response (ewma_stats.hpp)
 * - Volatility regime detection for signal gating
 * - Direction-agnostic feature signals (feature_signal.hpp)
 * - Dual statistics: rolling (stable) + EWMA (responsive)
 * - Adaptive window sizing (adaptive_window.hpp)
 * - Winsorization for outlier resistance (winsorization.hpp)
 * - End-to-end latency tracking with percentiles (latency_tracker.hpp)
 * 
 * See docs/ade_main_documentation.md for full details.
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
#include "../hpcm/simd_ops.hpp"
#include "tick_buffer.hpp"
#include "rolling_stats.hpp"
#include "ewma_stats.hpp"
#include "feature_signal.hpp"
#include "adaptive_window.hpp"
#include "winsorization.hpp"
#include "latency_tracker.hpp"
#include "normalizer.hpp"

using namespace sage;

// ============================================================================
// Configuration
// ============================================================================

constexpr size_t MAX_SYMBOLS = 256;
constexpr size_t BATCH_SIZE = 16;
constexpr int EWMA_HALF_LIFE = 50;      // Ticks for EWMA decay
constexpr int REGIME_HALF_LIFE = 100;   // Ticks for regime detection
constexpr int64_t MAX_ZSCORE = 3 * PRICE_SCALE;  // Z-score cap (winsorization)


// ============================================================================
// Global State (Pre-allocated)
// ============================================================================

// Ring buffers for inter-process communication
static RingBuffer<SageMessage, 65536> g_cal_to_ade_buffer;
static RingBuffer<SageMessage, 65536> g_ade_to_mind_buffer;

// Z-score capper for winsorization (outlier resistance)
static ade::ZScoreCapper g_zscore_capper(MAX_ZSCORE);

// Latency tracker for end-to-end metrics
static ade::LatencyTracker g_latency_tracker;

/**
 * Per-symbol analytics state
 * 
 * Combines rolling stats (stable, interpretable) with EWMA (responsive).
 * Regime detector gates signals during volatility spikes.
 * 
 * Memory layout optimized for cache efficiency:
 * - alignas(CACHE_LINE_SIZE) prevents false sharing between symbols
 */
struct alignas(CACHE_LINE_SIZE) SymbolState {
    // Traditional rolling statistics (stable baseline)
    ade::TickBuffer<256> ticks;           ///< Recent ticks for momentum analysis
    ade::RollingStats<64> price_stats;    ///< Rolling price statistics
    ade::RollingStats<64> volume_stats;   ///< Rolling volume statistics
    
    // EWMA statistics (faster regime response)
    ade::EWMAStats price_ewma;            ///< EWMA price for faster response
    ade::EWMAStats vol_ewma;              ///< EWMA volatility
    
    // Regime detection
    ade::VolRegimeDetector regime_detector; ///< Detects volatility regime changes
    
    // Metadata
    uint64_t last_update_ns;              ///< Timestamp of last update
    uint64_t message_count;               ///< Total messages processed
    
    // Constructor for proper initialization
    SymbolState() 
        : price_ewma(EWMA_HALF_LIFE)
        , vol_ewma(EWMA_HALF_LIFE)
        , regime_detector(REGIME_HALF_LIFE)
        , last_update_ns(0)
        , message_count(0) {}
};

// Compile-time verification of cache alignment
static_assert(alignof(SymbolState) == CACHE_LINE_SIZE, 
              "SymbolState must be cache-line aligned");

static std::array<SymbolState, MAX_SYMBOLS> g_symbol_states;


// Metrics
static std::atomic<uint64_t> g_messages_processed{0};
static std::atomic<uint64_t> g_signals_generated{0};
static std::atomic<uint64_t> g_signals_gated{0};     // Signals suppressed by regime/winsorization
static std::atomic<uint64_t> g_outliers_capped{0};   // Z-scores that were capped

// Sequence counter
static uint64_t g_sequence = 0;

// TSC calibrator
static timing::TSCCalibrator g_tsc_calibrator;

// ============================================================================
// Hot Path Processing
// ============================================================================

/**
 * Process incoming market data tick
 * 
 * Latency target: ~100ns total (includes EWMA, regime detection, winsorization)
 * 
 * Features:
 * - Dual z-score: rolling (stable) + EWMA (responsive)
 * - Winsorization: z-score capped to ±3σ
 * - Regime gating: signals suppressed during regime changes
 * - Latency tracking: end-to-end and processing time
 */
SAGE_HOT SAGE_FLATTEN
static void process_market_data(const SageMessage& msg) noexcept {
    const uint64_t start_tsc = timing::rdtsc();
    
    const auto& data = msg.payload.market_data;
    
    // Symbol lookup via bitmask
    // Note: CAL layer should validate symbol_id < MAX_SYMBOLS
    const size_t symbol_idx = data.symbol_id & (MAX_SYMBOLS - 1);
    
    SymbolState& state = g_symbol_states[symbol_idx];
    
    // ========================================
    // Update all statistics (O(1) each)
    // ========================================
    
    // Traditional rolling stats (stable, interpretable)
    state.ticks.push(data.price, data.quantity);
    state.price_stats.update(data.price.raw());
    state.volume_stats.update(data.quantity.raw());
    
    // EWMA stats (faster regime response)
    state.price_ewma.update(data.price.raw());
    state.vol_ewma.update(data.quantity.raw());
    
    // Update metadata
    state.last_update_ns = msg.timestamp_ns;
    state.message_count++;
    
    // ========================================
    // Calculate features
    // ========================================
    
    // Z-score from rolling stats (stable)
    const int64_t mean_price = state.price_stats.mean();
    const int64_t price_deviation = data.price.raw() - mean_price;
    const int64_t stddev = state.price_stats.stddev_approx();
    int64_t z_score = (stddev > 0) ? (price_deviation * PRICE_SCALE / stddev) : 0;
    
    // Winsorization: cap z-score to prevent outlier dominance
    if (g_zscore_capper.is_outlier(z_score)) {
        g_outliers_capped.fetch_add(1, std::memory_order_relaxed);
        z_score = g_zscore_capper.cap(z_score);
    }
    
    // Z-score from EWMA (responsive)
    const int64_t ewma_mean = state.price_ewma.mean();
    const int64_t ewma_dev = data.price.raw() - ewma_mean;
    const int64_t ewma_stddev = state.price_ewma.stddev_approx();
    int64_t z_score_ewma = (ewma_stddev > 0) ? (ewma_dev * PRICE_SCALE / ewma_stddev) : 0;
    z_score_ewma = g_zscore_capper.cap(z_score_ewma);  // Also cap EWMA z-score
    
    // Volatility and regime detection
    const int64_t current_var = state.price_stats.variance();
    bool regime_change = state.regime_detector.update(current_var);
    
    // Determine market regime
    ade::MarketRegime regime = ade::MarketRegime::NORMAL;
    if (regime_change) {
        regime = ade::MarketRegime::REGIME_CHANGE;
    } else if (state.regime_detector.current_vol() > 2 * PRICE_SCALE) {
        regime = ade::MarketRegime::HIGH_VOL;
    } else if (state.regime_detector.current_vol() < PRICE_SCALE / 2) {
        regime = ade::MarketRegime::LOW_VOL;
    }
    
    // ========================================
    // Generate legacy signal (backward compatible)
    // ========================================
    
    // Gate signals during regime changes
    bool should_signal = std::abs(z_score) > PRICE_SCALE / 2 && 
                         regime != ade::MarketRegime::REGIME_CHANGE;
    
    if (should_signal) {
        Signal sig;
        sig.symbol_id = data.symbol_id;
        sig.direction = (z_score > 0) ? 1 : -1;  // Deviation sign
        sig.confidence = FixedPoint(std::abs(z_score));
        sig.strategy_id = 1;  // Mean-reversion strategy

        
        SageMessage out_msg = SageMessage::create_signal(
            timing::get_monotonic_ns(),
            ++g_sequence,
            sig
        );
        
        if (g_ade_to_mind_buffer.try_push(out_msg)) {
            g_signals_generated.fetch_add(1, std::memory_order_relaxed);
        }
    } else if (std::abs(z_score) > PRICE_SCALE / 2) {
        // Would have signaled but was gated
        g_signals_gated.fetch_add(1, std::memory_order_relaxed);
    }

    // ========================================
    // Latency tracking
    // ========================================
    const uint64_t end_tsc = timing::rdtsc();
    g_latency_tracker.record_processing(start_tsc, end_tsc);
    
    // End-to-end latency (from message timestamp)
    uint64_t now_ns = timing::get_monotonic_ns();
    g_latency_tracker.record_e2e(msg.timestamp_ns, now_ns);
    

    // Track latency
    const uint64_t latency_tsc = timing::rdtsc() - start_tsc;
    g_total_latency_ns.fetch_add(
        g_tsc_calibrator.tsc_to_ns(latency_tsc),
        std::memory_order_relaxed
    );
    
    g_messages_processed.fetch_add(1, std::memory_order_relaxed);
}

// ============================================================================
// Batch Processing
// ============================================================================

SAGE_HOT
static size_t process_batch() noexcept {
    SageMessage batch[BATCH_SIZE];
    
    size_t count = g_cal_to_ade_buffer.try_pop_batch(batch, BATCH_SIZE);
    
    for (size_t i = 0; i < count; ++i) {
        // Prefetch next message
        if (i + 1 < count) {
            SAGE_PREFETCH_READ(&batch[i + 1]);
        }
        
        if (batch[i].msg_type == MessageType::MARKET_DATA) {
            process_market_data(batch[i]);
        } else if (batch[i].msg_type == MessageType::HEARTBEAT) {
            // Forward heartbeat
            g_ade_to_mind_buffer.try_push(batch[i]);
        }
    }
    
    return count;
}

// ============================================================================
// Heartbeat Thread
// ============================================================================

static void heartbeat_thread() {
    cpu::pin_to_core(CORE_OS);
    
    while (!ShutdownManager::instance().is_shutdown_requested()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        uint64_t processed = g_messages_processed.load();
        uint64_t signals = g_signals_generated.load();
        uint64_t gated = g_signals_gated.load();
        uint64_t outliers = g_outliers_capped.load();
        
        // Get latency summary
        auto latency_summary = g_latency_tracker.summary();
        
        std::cout << "[ADE] Stats: processed=" << processed
                  << " signals=" << signals
                  << " gated=" << gated
                  << " outliers=" << outliers
                  << " queue=" << g_cal_to_ade_buffer.size_approx()
                  << std::endl;
        
        std::cout << "[ADE] Latency: p50=" << latency_summary.e2e_p50 << "ns"
                  << " p99=" << latency_summary.e2e_p99 << "ns"
                  << " p99.9=" << latency_summary.e2e_p999 << "ns"
                  << " proc_mean=" << latency_summary.processing_mean << "ns"
                  << std::endl;
    }
}


// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "[ADE] Starting Analytics & Decision Engine..." << std::endl;
    
    // Initialize pre-allocated state
    for (auto& state : g_symbol_states) {
        state.last_update_ns = 0;
        state.message_count = 0;
    }
    
    // Pin to designated core
    if (cpu::pin_to_core(CORE_ADE) == 0) {
        std::cout << "[ADE] Pinned to core " << CORE_ADE << std::endl;
    }
    
    // Try real-time priority
    if (cpu::set_realtime_priority(50) == 0) {
        std::cout << "[ADE] Real-time priority set" << std::endl;
    }
    
    ShutdownManager::instance().install_signal_handlers();
    
    // Start heartbeat
    std::thread hb_thread(heartbeat_thread);
    
    std::cout << "[ADE] Entering main loop..." << std::endl;
    
    // Main processing loop
    while (!ShutdownManager::instance().is_shutdown_requested()) {
        size_t processed = process_batch();
        
        if (processed == 0) [[unlikely]] {
            // No work - yield briefly
            cpu::pause();
        }
    }
    
    std::cout << "[ADE] Shutting down..." << std::endl;
    hb_thread.join();
    
    // Final stats
    std::cout << "[ADE] Final: processed=" << g_messages_processed.load()
              << " signals=" << g_signals_generated.load()
              << std::endl;
    
    return 0;
}
