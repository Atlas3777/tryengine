#pragma once

#include <condition_variable>
#include <coroutine>
#include <exception>
#include <mutex>
#include <utility>

#include "engine/async/Task.hpp"
#include "engine/core/Error.hpp"
#include "engine/core/Expected.hpp"

namespace tryengine::async {

using core::Error;
using core::ErrorCode;

namespace detail {

struct SyncWaitEvent {
    std::mutex mutex;
    std::condition_variable cv;
    bool ready = false;

    void Wait() {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [this] { return ready; });
    }

    void SignalReady() {
        std::lock_guard<std::mutex> lock(mutex);
        ready = true;
        cv.notify_one();
    }
};

template <typename T>
struct SyncWaitPromise;

template <typename T>
struct SyncWaitTask {
    using promise_type = SyncWaitPromise<T>;

    explicit SyncWaitTask(std::coroutine_handle<promise_type> h) noexcept : handle_(h) {}
    SyncWaitTask(SyncWaitTask&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
    SyncWaitTask(const SyncWaitTask&) = delete;
    ~SyncWaitTask() {
        if (handle_) {
            handle_.destroy();
        }
    }

    void Start(SyncWaitEvent& event) {
        handle_.promise().event = &event;
        handle_.resume();
    }

    Result<T>& ResultRef() { return handle_.promise().result; }

private:
    std::coroutine_handle<promise_type> handle_;
};

template <typename T>
struct SyncWaitPromise {
    Result<T> result;
    SyncWaitEvent* event = nullptr;

    SyncWaitTask<T> get_return_object() noexcept {
        return SyncWaitTask<T>{std::coroutine_handle<SyncWaitPromise>::from_promise(*this)};
    }

    std::suspend_always initial_suspend() noexcept { return {}; }

    struct FinalAwaiter {
        bool await_ready() noexcept { return false; }

        void await_suspend(std::coroutine_handle<SyncWaitPromise> h) noexcept {
            // Последнее прикосновение к фрейму с этой стороны — сразу за этим
            // ждущий поток может на законных основаниях всё разрушить.
            h.promise().event->SignalReady();
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
            result = std::unexpected(Error("unknown exception in SyncWait driver"));
        }
    }
};

template <typename T>
SyncWaitTask<T> MakeSyncWaitTask(Task<T> task) {
    auto r = co_await task;
    co_return r;
}

}  // namespace detail

template <typename T>
Result<T> SyncWait(Task<T> task) {
    detail::SyncWaitEvent event;
    detail::SyncWaitTask<T> wrapper = detail::MakeSyncWaitTask(std::move(task));

    wrapper.Start(event);
    event.Wait();

    return std::move(wrapper.ResultRef());
}

}  // namespace tryengine::async
