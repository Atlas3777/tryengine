#pragma once

#include <EASTL/vector.h>
#include <atomic>
#include <coroutine>
#include <mutex>

namespace tryengine::resources {

enum class ResourceState : uint8_t { Empty, Loading, Ready, Failed };

template <typename T>
struct ResourceControlBlock {
    ~ResourceControlBlock() {
        delete data;
    }

    T* data{nullptr};
    std::atomic<ResourceState> state{ResourceState::Empty};
    std::atomic<uint32_t> ref_count{0};

    std::mutex waiters_mutex;
    eastl::vector<std::coroutine_handle<>> waiters;
};

}  // namespace tryengine::resources