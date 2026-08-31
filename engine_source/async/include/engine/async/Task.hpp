#pragma once

#include <coroutine>
#include <exception>
#include <optional>
#include <utility>

#include "engine/async/Executor.hpp"
#include "engine/core/Assert.hpp"
#include "engine/core/Error.hpp"
#include "engine/core/Log.hpp"
#include "engine/core/Result.hpp"

namespace tryengine::async {
using core::Error;
using core::ErrorCode;

template <typename T = void>
class Task;

namespace detail {

struct TaskPromiseBase {
    std::coroutine_handle<> continuation;
    Executor* origin_executor = nullptr;

    std::suspend_always initial_suspend() noexcept { return {}; }

    struct FinalAwaiter {
        bool await_ready() noexcept { return false; }

        template <typename Promise>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> h) noexcept {
            auto continuation = h.promise().continuation;
            if (!continuation) {
                return std::noop_coroutine();
            }

            Executor* origin_exec = h.promise().origin_executor;
            Executor* current_exec = GetCurrentExecutor();

            TRY_ASSERT(origin_exec != nullptr, "Task completed continuation with unknown origin executor — "
                                                "some resumption path is missing CurrentExecutorScope");

            if (origin_exec != current_exec) {
                origin_exec->Schedule(continuation);
                return std::noop_coroutine();
            }
            return continuation;
        }

        void await_resume() noexcept {}
    };

    FinalAwaiter final_suspend() noexcept { return {}; }
};

template <typename T>
struct TaskPromise final : TaskPromiseBase {
    std::optional<Result<T>> result;

    Task<T> get_return_object() noexcept;

    template <typename U>
        requires std::convertible_to<U, T>
    void return_value(U&& value) {
        result.emplace(std::forward<U>(value));
    }

    void return_value(Error error) { result.emplace(std::move(error)); }

    void return_value(Result<T> value) { result.emplace(std::move(value)); }

    void unhandled_exception() noexcept {
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
        try {
            std::rethrow_exception(std::current_exception());
        } catch (const std::exception& e) {
            result.emplace(Error(e.what()));
        } catch (...) {
            result.emplace(Error("Unknown exception in coroutine"));
        }
#else
        TRY_CHECK(false, "Unhandled exception reached coroutine in -fno-exceptions mode");
#endif
    }
};

template <>
struct TaskPromise<void> final : TaskPromiseBase {
    std::optional<Result<void>> result;

    Task<void> get_return_object() noexcept;

    void return_value(Error error) { result.emplace(std::move(error)); }

    void return_value(Result<void> value) { result.emplace(std::move(value)); }

    void unhandled_exception() noexcept {
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
        try {
            std::rethrow_exception(std::current_exception());
        } catch (const std::exception& e) {
            result.emplace(Error(e.what()));
        } catch (...) {
            result.emplace(Error("Unknown exception in coroutine"));
        }
#else
        TRY_CHECK(false, "Unhandled exception reached coroutine in -fno-exceptions mode");
#endif
    }
};

}  // namespace detail

template <typename T>
class [[nodiscard]] Task {
public:
    using promise_type = detail::TaskPromise<T>;
    using ResultType = Result<T>;

    Task() noexcept = default;

    explicit Task(std::coroutine_handle<promise_type> handle) noexcept : handle_(handle) {}

    Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            Destroy();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    ~Task() {
#ifndef NDEBUG
        if (handle_ && handle_.done()) {
            auto& promise = handle_.promise();
            if (promise.result.has_value()) {
                auto& res = *promise.result;
                if (!res.has_value()) {
                    core::LogCritical("Task destroyed with unchecked error: {}", res.error());
                    TRY_ASSERT(false, "Task containing Error was dropped without co_await!");
                }
            }
        }
#endif
        Destroy();
    }

    class Awaiter {
    public:
        explicit Awaiter(std::coroutine_handle<promise_type> handle) noexcept : handle_(handle) {}

        bool await_ready() const noexcept { return false; }

        std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
            handle_.promise().continuation = awaiting;
            handle_.promise().origin_executor = GetCurrentExecutor();
            return handle_;
        }

        ResultType await_resume() {
            TRY_ASSERT(handle_.promise().result.has_value(), "Task result accessed before completion");
            ResultType res = std::move(*handle_.promise().result);
            handle_.promise().result.reset();
            return res;
        }

    private:
        std::coroutine_handle<promise_type> handle_;
    };

    Awaiter operator co_await() & noexcept { return Awaiter(handle_); }
    Awaiter operator co_await() && noexcept { return Awaiter(handle_); }

    std::coroutine_handle<promise_type> NativeHandle() const noexcept { return handle_; }

private:
    void Destroy() {
        if (handle_) {
            handle_.destroy();
        }
    }

    std::coroutine_handle<promise_type> handle_;
};

template <typename T>
inline Task<T> detail::TaskPromise<T>::get_return_object() noexcept {
    return Task<T>{std::coroutine_handle<TaskPromise<T>>::from_promise(*this)};
}

inline Task<void> detail::TaskPromise<void>::get_return_object() noexcept {
    return Task<void>{std::coroutine_handle<TaskPromise<void>>::from_promise(*this)};
}

struct ExecutorSwitch {
    Executor& executor;

    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle) const { executor.Schedule(handle); }
    void await_resume() const noexcept {}
};

template <typename T>
Task<T> ScheduleOn(Executor& executor, Task<T> task) {
    co_await ExecutorSwitch{executor};
    co_return co_await task;
}

}  // namespace tryengine::async