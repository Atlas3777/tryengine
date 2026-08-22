#pragma once

#include <EASTL/vector.h>
#include <atomic>
#include <coroutine>
#include <memory>
#include <mutex>

namespace tryengine::resources {

enum class ResourceState : uint8_t { Empty, Loading, Ready, Failed };

template <typename T>
struct ResourceControlBlock {
    std::unique_ptr<T> data{nullptr};
    std::atomic<ResourceState> state{ResourceState::Empty};

    std::mutex waiters_mutex;
    eastl::vector<std::coroutine_handle<>> waiters;
};

}  // namespace tryengine::resources