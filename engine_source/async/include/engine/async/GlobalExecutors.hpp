#pragma once

#include <thread>

#include "engine/async/MainThreadExecutor.hpp"
#include "engine/async/ThreadPoolExecutor.hpp"

namespace tryengine::async {

// Единые на весь процесс executor'ы. Пробрасывать ThreadPoolExecutor&/
// MainThreadExecutor& через десяток сигнатур ради того, чтобы асинхронная задача
// где-то в глубине AssetSourceDatabase могла постнуть работу — неоправданная цена.
// Пул потоков и "выполнить в следующем Pull() на главном потоке" на процесс нужны
// ровно одни, так что делаем их синглтонами.
//
// inline-функция + function-local static: ленивая инициализация при первом
// обращении, потокобезопасна с C++11 и не зависит от порядка статической
// инициализации между единицами трансляции (в отличие от глобальной переменной).

inline ThreadPoolExecutor& ThreadPool() {
    static ThreadPoolExecutor instance(
        std::thread::hardware_concurrency() > 2 ? std::thread::hardware_concurrency() - 2 : 1);
    return instance;
}

inline MainThreadExecutor& MainThread() {
    static MainThreadExecutor instance;
    return instance;
}

}  // namespace tryengine::async