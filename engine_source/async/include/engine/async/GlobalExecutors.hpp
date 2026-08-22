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
    // eastl::string exec_name = "Unknown/RawThread";
    //
    // if (GetCurrentExecutor() == &MainThread()) {
    //     exec_name = "MainThread";
    // } else if (GetCurrentExecutor() == &ThreadPool()) {
    //     exec_name = "ThreadPool";
    // }
    // eastl::string str = fmt::format("Thread: {} | Exec: {}", std::this_thread::get_id(),exec_name);
    //
    // TRY_LOG_CRIT("{}", str);
}

}  // namespace tryengine::async