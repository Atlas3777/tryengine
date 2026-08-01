#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "engine/async/Executor.hpp"

namespace tryengine::async {

class ThreadPoolExecutor final : public Executor {
public:
    explicit ThreadPoolExecutor(std::size_t threadCount = std::thread::hardware_concurrency()) {
        if (threadCount == 0) {
            threadCount = 1;
        }

        workers_.reserve(threadCount);
        for (std::size_t i = 0; i < threadCount; ++i) {
            workers_.emplace_back([this] { WorkerLoop(); });
        }
    }

    ThreadPoolExecutor(const ThreadPoolExecutor&) = delete;
    ThreadPoolExecutor& operator=(const ThreadPoolExecutor&) = delete;

    ~ThreadPoolExecutor() override {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        cv_.notify_all();
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    void Post(std::move_only_function<void()> work) override {
        {
            std::lock_guard lock(mutex_);
            queue_.push(std::move(work));
        }
        cv_.notify_one();
    }

    [[nodiscard]] std::size_t ThreadCount() const noexcept { return workers_.size(); }

private:
    void WorkerLoop() {
        for (;;) {
            std::move_only_function<void()> work;
            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });

                if (stopping_ && queue_.empty()) {
                    return;
                }

                work = std::move(queue_.front());
                queue_.pop();
            }

            work();
        }
    }

    std::vector<std::thread> workers_;
    std::queue<std::move_only_function<void()>> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stopping_ = false;
};

}  // namespace tryengine::async
