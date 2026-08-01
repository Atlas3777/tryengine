// PollHandle.hpp
#pragma once

#include <atomic>
#include <memory>
#include <utility>

#include "engine/async/Executor.hpp"
#include "engine/async/Task.hpp"

namespace tryengine::async {

template <typename T>
struct PollState {
    std::atomic<bool> ready{false};
    Result<T> result;
};

template <typename T>
class [[nodiscard]] PollHandle {
public:
    PollHandle() noexcept = default;
    explicit PollHandle(std::shared_ptr<PollState<T>> state) noexcept : state_(std::move(state)) {}

    [[nodiscard]] bool IsReady() const noexcept { return state_ && state_->ready.load(std::memory_order_acquire); }
    [[nodiscard]] Result<T>& GetResult() & { return state_->result; }
    [[nodiscard]] Result<T>&& GetResult() && { return std::move(state_->result); }

private:
    std::shared_ptr<PollState<T>> state_;
};

namespace detail {

template <typename T>
struct PollDriverPromise {
    std::suspend_never initial_suspend() noexcept { return {}; }

    struct FinalAwaiter {
        bool await_ready() noexcept { return false; }
        void await_suspend(std::coroutine_handle<PollDriverPromise> h) noexcept { h.destroy(); }
        void await_resume() noexcept {}
    };
    FinalAwaiter final_suspend() noexcept { return {}; }

    void return_void() noexcept {}
    void unhandled_exception() noexcept {}  // сюда никогда не попадём: Task сама ловит исключения в TaskPromise

    struct Handle {
        using promise_type = PollDriverPromise;
        Handle(std::coroutine_handle<promise_type>) noexcept {}
    };
    Handle get_return_object() noexcept {
        return Handle{std::coroutine_handle<PollDriverPromise>::from_promise(*this)};
    }};

// state — обычный параметр корутины, никакого from_promise не нужно
template <typename T>
typename PollDriverPromise<T>::Handle DrivePoll(Task<T> task, std::shared_ptr<PollState<T>> state) {
    state->result = co_await task;
    state->ready.store(true, std::memory_order_release);
}

}  // namespace detail

template <typename T>
PollHandle<T> RunPollable(Executor& executor, Task<T> task) {
    auto state = std::make_shared<PollState<T>>();
    executor.Post([task = std::move(task), state]() mutable {
        detail::DrivePoll(std::move(task), state);  // стартует уже на потоке пула
    });
    return PollHandle<T>{state};
}

}  // namespace tryengine::async