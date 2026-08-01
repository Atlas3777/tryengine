#pragma once

#include <coroutine>
#include <utility>

#include "engine/async/Task.hpp"

namespace tryengine::async {

namespace detail {

struct RunAndForgetPromise {
    std::suspend_never initial_suspend() noexcept { return {}; }

    struct FinalAwaiter {
        bool await_ready() noexcept { return false; }

        void await_suspend(std::coroutine_handle<RunAndForgetPromise> h) noexcept { h.destroy(); }

        void await_resume() noexcept {}
    };

    FinalAwaiter final_suspend() noexcept { return {}; }

    void return_void() noexcept {}

    void unhandled_exception() noexcept {}

    std::coroutine_handle<RunAndForgetPromise> get_return_object() noexcept {
        return std::coroutine_handle<RunAndForgetPromise>::from_promise(*this);
    }
};

struct RunAndForgetHandle {
    using promise_type = RunAndForgetPromise;

    RunAndForgetHandle(std::coroutine_handle<promise_type>) noexcept {}
};

template <typename T>
RunAndForgetHandle RunAndForgetImpl(Task<T> task) {
    co_await task;
}

}  // namespace detail

// Свободная функция — то же самое, что executor.RunAndForget(task), для тех, кому
// привычнее не-member стиль. Executor обязателен: спрятать его снова, как раньше,
// умышленно нельзя — иначе снова неочевидно, на каком потоке всё стартует.
template <typename T>
void RunAndForget(Executor& executor, Task<T> task) {
    executor.RunAndForget(std::move(task));
}

}  // namespace tryengine::async

// Определение Executor::Execute — здесь, а не в Executor.hpp, потому что здесь
// уже виден полный Task<T> (в Executor.hpp он только forward-declared).
template <typename T>
void Executor::Execute(tryengine::async::Task<T> task) {
    Post([task = std::move(task)]() mutable {
        tryengine::async::detail::RunAndForgetImpl(std::move(task));
    });
}