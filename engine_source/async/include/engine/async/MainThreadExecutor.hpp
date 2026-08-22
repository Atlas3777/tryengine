#pragma once

#include <mutex>
#include <vector>
#include <coroutine>
#include <utility>

#include "engine/async/Executor.hpp"

namespace tryengine::async {

class MainThreadExecutor final : public Executor {
public:
    void Post(std::move_only_function<void()> work) override {
        std::lock_guard lock(mutex_);
        closures_.push_back(std::move(work));
    }

    void Schedule(std::coroutine_handle<> handle) override {
        std::lock_guard lock(mutex_);
        handles_.push_back(handle);
    }

    void Pull() {
        std::vector<std::move_only_function<void()>> local_closures;
        std::vector<std::coroutine_handle<>> local_handles;

        {
            std::lock_guard lock(mutex_);
            local_closures.swap(closures_);
            local_handles.swap(handles_);
        }

        CurrentExecutorScope scope(this);

        for (auto& work : local_closures) {
            work();
        }
        for (auto handle : local_handles) {
            handle.resume();
        }
    }

private:
    std::vector<std::move_only_function<void()>> closures_;
    std::vector<std::coroutine_handle<>> handles_;
    mutable std::mutex mutex_;
};

}  // namespace tryengine::async