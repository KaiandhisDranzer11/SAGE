#pragma once

/**
 * SAGE Graceful Shutdown Manager
 * Lock-free signal handling for deterministic shutdown
 */

#include <atomic>
#include <csignal>
#include <functional>
#include <vector>

#include "compiler.hpp"

namespace sage {

/**
 * Singleton shutdown coordinator
 * Thread-safe, lock-free shutdown signaling
 */
class ShutdownManager {
public:
    static ShutdownManager& instance() noexcept {
        static ShutdownManager instance;
        return instance;
    }
    
    /**
     * Register shutdown handler (called in reverse order)
     * NOT thread-safe - call only during initialization
     */
    void register_handler(std::function<void()> handler) {
        handlers_.push_back(std::move(handler));
    }
    
    /**
     * Signal shutdown and execute handlers
     * Thread-safe, idempotent (only runs once)
     */
    void signal_shutdown() noexcept {
        bool expected = false;
        if (shutdown_requested_.compare_exchange_strong(
                expected, true, 
                std::memory_order_acq_rel,
                std::memory_order_relaxed)) {
            
            // Execute handlers in reverse order
            for (auto it = handlers_.rbegin(); it != handlers_.rend(); ++it) {
                try {
                    (*it)();
                } catch (...) {
                    // Swallow exceptions during shutdown
                }
            }
        }
    }
    
    /**
     * Check if shutdown has been requested
     * Lock-free, suitable for hot path
     */
    SAGE_ALWAYS_INLINE bool is_shutdown_requested() const noexcept {
        return shutdown_requested_.load(std::memory_order_acquire);
    }
    
    /**
     * Install signal handlers for SIGINT/SIGTERM
     */
    void install_signal_handlers() noexcept {
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);
    }

private:
    ShutdownManager() = default;
    ~ShutdownManager() = default;
    
    // Non-copyable
    ShutdownManager(const ShutdownManager&) = delete;
    ShutdownManager& operator=(const ShutdownManager&) = delete;
    
    static void signal_handler(int) noexcept {
        instance().signal_shutdown();
    }
    
    SAGE_CACHE_ALIGNED std::atomic<bool> shutdown_requested_{false};
    std::vector<std::function<void()>> handlers_;
};

/**
 * RAII shutdown registration helper
 */
class ShutdownGuard {
public:
    explicit ShutdownGuard(std::function<void()> handler) {
        ShutdownManager::instance().register_handler(std::move(handler));
    }
};

} // namespace sage
