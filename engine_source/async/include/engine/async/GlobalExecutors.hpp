#pragma once

#include <thread>

#include "engine/async/MainThreadExecutor.hpp"
#include "engine/async/ThreadPoolExecutor.hpp"

namespace tryengine::async {

inline ThreadPoolExecutor& ThreadPool() {
    static ThreadPoolExecutor instance(
        std::thread::hardware_concurrency() > 2 ? std::thread::hardware_concurrency() - 2 : 1);
    return instance;
}

inline MainThreadExecutor& MainThread() {
    static MainThreadExecutor instance;
    return instance;
}


inline void GetCurrentContextInfo() {
    if (GetCurrentExecutor() == &MainThread()) {
        LogInfo("MainThread | Exec: {}", std::this_thread::get_id());
    } else {
        LogInfo("ThreadPool | Exec: {}", std::this_thread::get_id());
    }
}

}  // namespace tryengine::async