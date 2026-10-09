#pragma once
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace portbridge::detail {
// One persistent worker, at most one executing task and one queued task.
// Tasks own their state/paths and must never capture a QWidget. Publishing a
// result does not imply OS thread termination; the GUI never joins this worker.
class BackgroundWorker {
  public:
    using Task = std::function<void()>;
    BackgroundWorker() : state_(std::make_shared<State>()) {}
    BackgroundWorker(const BackgroundWorker &) = delete;
    BackgroundWorker &operator=(const BackgroundWorker &) = delete;
    ~BackgroundWorker() { stop(std::chrono::milliseconds(0)); }

    bool submit(Task task) {
        const auto state = state_;
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!task || state->stopping || state->pending)
            return false;
        state->pending = std::move(task);
        if (!state->started) {
            try {
                std::thread([state] { run(state); }).detach();
                state->started = true;
            } catch (...) {
                state->pending = {};
                return false;
            }
        }
        state->changed.notify_one();
        return true;
    }

    // Return after the task loop exits, or at the declared shutdown budget.
    // A stalled task keeps only State alive; there is no GUI-owned thread handle
    // to join, even if thread-local/OS teardown stalls after task publication.
    bool stop(std::chrono::milliseconds timeout) {
        const auto state = state_;
        std::unique_lock<std::mutex> lock(state->mutex);
        state->stopping = true;
        state->pending = {};
        state->changed.notify_all();
        return !state->started || state->changed.wait_for(lock, timeout, [&] { return state->exited; });
    }

  private:
    struct State {
        std::mutex mutex;
        std::condition_variable changed;
        Task pending;
        bool started = false, stopping = false, exited = false;
    };
    static void run(const std::shared_ptr<State> &state) {
        for (;;) {
            Task task;
            {
                std::unique_lock<std::mutex> lock(state->mutex);
                state->changed.wait(lock, [&] { return state->stopping || bool(state->pending); });
                if (state->stopping) {
                    state->exited = true;
                    state->changed.notify_all();
                    return;
                }
                task = std::move(state->pending);
                state->pending = {};
            }
            try {
                task();
            } catch (...) { /* Job owns its error/result reporting. */
            }
            // Task resources and filesystem objects are destroyed on the worker.
        }
    }
    std::shared_ptr<State> state_;
};
} // namespace portbridge::detail
