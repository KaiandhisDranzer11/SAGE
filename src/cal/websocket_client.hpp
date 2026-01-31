#pragma once

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <chrono>

// Mocking boost::asio includes for structure
// #include <boost/asio.hpp>
// #include <boost/beast.hpp>

namespace sage {
namespace cal {

class WebSocketClient {
public:
    using MessageCallback = std::function<void(const char* data, size_t len)>;

    WebSocketClient(const std::string& uri, MessageCallback callback) 
        : uri_(uri), callback_(callback), running_(false) {}

    void start() {
        running_ = true;
        thread_ = std::thread(&WebSocketClient::io_loop, this);
    }

    void stop() {
        running_ = false;
        if (thread_.joinable()) {
            thread_.join();
        }
    }

private:
    std::string uri_;
    MessageCallback callback_;
    std::atomic<bool> running_;
    std::thread thread_;

    void io_loop() {
        // Pin to Core 1 implementation
        // ...
        
        int backoff_ms = 100;
        
        while (running_) {
            try {
                // Connect logic...
                // Handshake...
                // Read loop...
                    // On message: callback_(buffer, len);
                    
                // If disconnect:
                throw std::runtime_error("Disconnected");
                
            } catch (...) {
                if (!running_) break;
                
                // Exponential backoff
                std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
                backoff_ms = std::min(backoff_ms * 2, 30000); // Capped at 30s
            }
        }
    }
};

} // namespace cal
} // namespace sage
