#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace portbridge::detail {
// The worker owns this state independently of any QWidget. Keeping completion
// notification under the mutex avoids a lost wakeup during bounded shutdown.
struct BackgroundCompletion {
    std::atomic<bool> finished{false};
    std::mutex completionMutex;
    std::condition_variable completionChanged;
    void complete() {
        { std::lock_guard<std::mutex> lock(completionMutex); finished.store(true,std::memory_order_release); }
        completionChanged.notify_all();
    }
    bool waitFor(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(completionMutex);
        return completionChanged.wait_for(lock,timeout,[this]{return finished.load(std::memory_order_acquire);});
    }
};
}
