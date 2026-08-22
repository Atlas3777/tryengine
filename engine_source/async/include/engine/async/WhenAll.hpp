#pragma once

#include <EASTL/tuple.h>
#include <EASTL/vector.h>
#include <atomic>
#include <coroutine>
#include <exception>
#include <utility>

#include "engine/async/Task.hpp"
#include "engine/core/Error.hpp"
#include "engine/core/Result.hpp"

namespace tryengine::async {

using core::Error;

namespace detail {

class WhenAllCounter {
public:
    explicit WhenAllCounter(size_t count) noexcept : count_(count + 1) {}

    bool TryAwait(std::coroutine_handle<> continuation) noexcept {
        continuation_ = continuation;
        return count_.fetch_sub(1, std::memory_order_acq_rel) > 1;
    }

    void NotifyCompleted() noexcept {
        if (count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            continuation_.resume();
        }
    }

private:
    std::atomic<std::size_t> count_;
    std::coroutine_handle<> continuation_;
};

struct WhenAllReadyAwaiter {
    WhenAllCounter& counter;

    bool await_ready() noexcept { return false; }
    bool await_suspend(std::coroutine_handle<> awaiting) noexcept { return counter.TryAwait(awaiting); }
    void await_resume() noexcept {}
};

template <typename T>
struct WhenAllTaskPromise;

template <typename T>
struct WhenAllTask {
    using promise_type = WhenAllTaskPromise<T>;

    explicit WhenAllTask(std::coroutine_handle<promise_type> h) noexcept : handle_(h) {}
    WhenAllTask(WhenAllTask&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
    WhenAllTask(const WhenAllTask&) = delete;
    ~WhenAllTask() {
        if (handle_) {
            handle_.destroy();
        }
    }

    void Start(WhenAllCounter& counter) {
        handle_.promise().counter = &counter;
        handle_.resume();
    }

    Result<T>& ResultRef() {
        TRY_ASSERT(handle_.promise().result.has_value(), "WhenAll subtask result accessed before completion");
        return *handle_.promise().result;
    }

private:
    std::coroutine_handle<promise_type> handle_;
};

template <typename T>
struct WhenAllTaskPromise {
    std::optional<Result<T>> result;
    WhenAllCounter* counter = nullptr;

    WhenAllTask<T> get_return_object() noexcept {
        return WhenAllTask<T>{std::coroutine_handle<WhenAllTaskPromise>::from_promise(*this)};
    }

    std::suspend_always initial_suspend() noexcept { return {}; }

    struct FinalAwaiter {
        bool await_ready() noexcept { return false; }
        void await_suspend(std::coroutine_handle<WhenAllTaskPromise> h) noexcept {
            h.promise().counter->NotifyCompleted();
        }
        void await_resume() noexcept {}
    };

    FinalAwaiter final_suspend() noexcept { return {}; }

    void return_value(Result<T> value) { result = std::move(value); }

    void unhandled_exception() noexcept {
        try {
            std::rethrow_exception(std::current_exception());
        } catch (const std::exception& e) {
            result = std::unexpected(Error(e.what()));
        } catch (...) {
            result = std::unexpected(Error("unknown exception in WhenAll subtask"));
        }
    }
};

template <typename T>
WhenAllTask<T> MakeWhenAllTask(Task<T> task) {
    auto r = co_await task;
    co_return r;
}

}  // namespace detail

// ---- Однотипный набор задач, размер известен только в рантайме ----------
template <typename T>
Task<eastl::vector<Result<T>>> WhenAll(eastl::vector<Task<T>> tasks) {
    eastl::vector<detail::WhenAllTask<T>> wrapped;
    wrapped.reserve(tasks.size());
    for (auto& task : tasks) {
        wrapped.push_back(detail::MakeWhenAllTask(eastl::move(task)));
    }

    detail::WhenAllCounter counter(wrapped.size());
    for (auto& t : wrapped) {
        t.Start(counter);
    }

    if (!wrapped.empty()) {
        co_await detail::WhenAllReadyAwaiter{counter};
    }

    eastl::vector<Result<T>> results;
    results.reserve(wrapped.size());
    for (auto& t : wrapped) {
        results.push_back(eastl::move(t.ResultRef()));
    }
    co_return results;
}

template <typename... Ts>
Task<eastl::tuple<Result<Ts>...>> WhenAll(Task<Ts>... tasks) {
    eastl::tuple<detail::WhenAllTask<Ts>...> wrapped{detail::MakeWhenAllTask(eastl::move(tasks))...};

    detail::WhenAllCounter counter(sizeof...(Ts));
    eastl::apply([&counter](auto&... t) { (t.Start(counter), ...); }, wrapped);

    if constexpr (sizeof...(Ts) > 0) {
        co_await detail::WhenAllReadyAwaiter{counter};
    }

    co_return eastl::apply([](auto&... t) { return eastl::make_tuple(eastl::move(t.ResultRef())...); }, wrapped);
}

// ---- То же самое, но каждая подзадача реально стартует на executor'е ------------
//
// Обычный WhenAll резюмит подзадачи по очереди на текущем потоке — пока каждая не
// дойдёт до своей первой точки co_await. Если синхронный "пролог" таски что-то
// весит (парсинг, компрессия, импорт и т.п.), весь этот вес выполняется
// последовательно на одном потоке, и параллелизм пула никак не используется.
//
// Эта перегрузка честно постит запуск каждой подзадачи через executor.Post(),
// поэтому прологи расходятся по разным потокам пула и реально выполняются
// параллельно. Платится за это одна аллокация на Post() на подзадачу — обычно
// того стоит, если сами подзадачи не тривиальны.
template <typename T>
Task<eastl::vector<Result<T>>> WhenAll(Executor& executor, eastl::vector<Task<T>> tasks) {
    eastl::vector<detail::WhenAllTask<T>> wrapped;
    wrapped.reserve(tasks.size());
    for (auto& task : tasks) {
        wrapped.push_back(detail::MakeWhenAllTask(eastl::move(task)));
    }

    detail::WhenAllCounter counter(wrapped.size());
    for (auto& t : wrapped) {
        executor.Post([&t, &counter] { t.Start(counter); });
    }

    if (!wrapped.empty()) {
        co_await detail::WhenAllReadyAwaiter{counter};
    }

    eastl::vector<Result<T>> results;
    results.reserve(wrapped.size());
    for (auto& t : wrapped) {
        results.push_back(eastl::move(t.ResultRef()));
    }
    co_return results;
}

template <typename... Ts>
Task<eastl::tuple<Result<Ts>...>> WhenAll(Executor& executor, Task<Ts>... tasks) {
    eastl::tuple<detail::WhenAllTask<Ts>...> wrapped{detail::MakeWhenAllTask(eastl::move(tasks))...};

    detail::WhenAllCounter counter(sizeof...(Ts));
    eastl::apply([&executor, &counter](auto&... t) { (executor.Post([&t, &counter] { t.Start(counter); }), ...); },
                 wrapped);

    if constexpr (sizeof...(Ts) > 0) {
        co_await detail::WhenAllReadyAwaiter{counter};
    }

    co_return eastl::apply([](auto&... t) { return eastl::make_tuple(eastl::move(t.ResultRef())...); }, wrapped);
}

}  // namespace tryengine::async