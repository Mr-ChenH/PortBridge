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
// A stalled filesystem cannot make window destruction wait indefinitely.
// Detached workers must capture their state/paths by value and own no UI object.
inline bool finishBackgroundThread(std::thread& worker,const std::shared_ptr<BackgroundCompletion>& state,
                                   std::chrono::milliseconds timeout) {
    if(!worker.joinable())return true;
    if(state && state->waitFor(timeout)) { worker.join(); return true; }
    worker.detach();return false;
}
}
