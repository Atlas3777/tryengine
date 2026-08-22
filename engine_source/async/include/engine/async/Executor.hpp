#pragma once

#include <bits/move_only_function.h>
#include <coroutine>

namespace tryengine::async {
template <typename T>
class Task;
}  // namespace tryengine::async

class Executor {
public:
    virtual ~Executor() = default;

    virtual void Post(std::move_only_function<void()> work) = 0;

    virtual void Schedule(std::coroutine_handle<> handle) {
        Post([handle] { handle.resume(); });
    }

    template <typename T>
    void Execute(tryengine::async::Task<T> task);

    // Просто более читаемое на месте вызова имя для Execute — то же самое.
    template <typename T>
    void RunAndForget(tryengine::async::Task<T> task) {
        Execute(std::move(task));
    }
};

namespace tryengine::async {

inline thread_local Executor* g_current_executor = nullptr;

inline Executor* GetCurrentExecutor() noexcept {
    return g_current_executor;
}

struct CurrentExecutorScope {
    Executor* prev_;
    explicit CurrentExecutorScope(Executor* exec) noexcept : prev_(g_current_executor) {
        g_current_executor = exec;
    }
    ~CurrentExecutorScope() {
        g_current_executor = prev_;
    }
};

} // namespace tryengine::async