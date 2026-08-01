#pragma once

#include <coroutine>
#include <exception>
#include <utility>

#include "engine/async/Executor.hpp"
#include "engine/core/Error.hpp"
#include "engine/core/Expected.hpp"

namespace tryengine::async {
using core::Error;
using core::ErrorCode;

template <typename T = void>
class Task;

namespace detail {

struct TaskPromiseBase {
    std::coroutine_handle<> continuation;

    std::suspend_always initial_suspend() noexcept { return {}; }

    struct FinalAwaiter {
        bool await_ready() noexcept { return false; }

        template <typename Promise>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> h) noexcept {
            auto continuation = h.promise().continuation;
            return continuation ? continuation : std::noop_coroutine();
        }

        void await_resume() noexcept {}
    };

    FinalAwaiter final_suspend() noexcept { return {}; }
};

template <typename T>
struct TaskPromise final : TaskPromiseBase {
    Result<T> result;

    Task<T> get_return_object() noexcept;

    template <typename U>
        requires std::convertible_to<U, T>
    void return_value(U&& value) {
        result.emplace(std::forward<U>(value));
    }

    void return_value(Error error) { result = std::unexpected(std::move(error)); }

    void return_value(Result<T> value) { result = std::move(value); }

    void return_value(ErrorCode code) { result = std::unexpected(Error(code)); }

    void unhandled_exception() noexcept {
        try {
            std::rethrow_exception(std::current_exception());
        } catch (const std::exception& e) {
            result = std::unexpected(Error(ErrorCode::EngineInternalError, e.what()));
        } catch (...) {
            result = std::unexpected(Error(ErrorCode::EngineInternalError, "Unknown exception in Task<T>."));
        }
    }
};

template <>
struct TaskPromise<void> final : TaskPromiseBase {
    Result<void> result;

    Task<void> get_return_object() noexcept;

    void return_value(Error error) { result = std::unexpected(std::move(error)); }

    void return_value(Result<void> value) { result = std::move(value); }

    void return_value(ErrorCode code) { result = std::unexpected(Error(code)); }

    void unhandled_exception() noexcept {
        try {
            std::rethrow_exception(std::current_exception());
        } catch (const std::exception& e) {
            result = std::unexpected(Error(ErrorCode::EngineInternalError, e.what()));
        } catch (...) {
            result = std::unexpected(Error(ErrorCode::EngineInternalError, "Unknown exception in Task<void>."));
        }
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

    ~Task() { Destroy(); }

    class Awaiter {
    public:
        explicit Awaiter(std::coroutine_handle<promise_type> handle) noexcept : handle_(handle) {}

        bool await_ready() const noexcept { return false; }

        std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
            handle_.promise().continuation = awaiting;
            return handle_;
        }

        ResultType await_resume() { return std::move(handle_.promise().result); }

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
