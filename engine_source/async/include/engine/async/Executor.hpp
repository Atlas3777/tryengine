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

    // Fire-and-forget: запускает задачу целиком на этом Executor'е, в том числе
    // самый первый её шаг (синхронный "пролог" до первого co_await). В отличие от
    // "голого" co_await или свободной функции без executor'а, ни один код таски не
    // выполнится на вызывающем потоке.
    //
    // Определение — в RunAndForget.hpp, т.к. там уже виден полный Task<T>
    // (здесь он только forward-declared, чтобы не тащить Task.hpp -> Executor.hpp
    // -> Task.hpp по кругу). Поэтому Execute/RunAndForget по факту доступны только
    // в тех .cpp, куда включён RunAndForget.hpp — как и раньше.
    template <typename T>
    void Execute(tryengine::async::Task<T> task);

    // Просто более читаемое на месте вызова имя для Execute — то же самое.
    template <typename T>
    void RunAndForget(tryengine::async::Task<T> task) {
        Execute(std::move(task));
    }
};