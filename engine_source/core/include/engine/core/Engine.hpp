#pragma once

#include <utility>

#include "engine/core/Inline.hpp"
#include "engine/core/Assert.hpp"
#include "engine/core/TypeRegistry.hpp"

namespace tryengine::core {

class Engine {
public:
    static constexpr uint32_t MAX_SYSTEMS = 32;

    Engine() = default;
    ~Engine() { Shutdown(); }

    Engine(const Engine&)            = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&)                 = delete;
    Engine& operator=(Engine&&)      = delete;

    template <typename T, typename... Args>
    T& RegisterSystem(Args&&... args) {
        const uint32_t id = ScopedTypeId<EngineSystemCounter, T>::Value();
        TRY_ASSERT(id < MAX_SYSTEMS, "Too many systems registered with Engine!");
        TRY_ASSERT(instances_[id] == nullptr, "Double registration");

        T* system = new T(std::forward<Args>(args)...);

        instances_[id]  = system;
        destroyers_[id] = [](void* p) { delete static_cast<T*>(p); };
        return *system;
    }

    template <typename T>
    [[nodiscard]] TRY_INLINE T* TryGet() const noexcept {
        const uint32_t id = ScopedTypeId<EngineSystemCounter, T>::Value();
        TRY_ASSERT(id < MAX_SYSTEMS, "Max system count limit above");
        return (id < MAX_SYSTEMS) ? static_cast<T*>(instances_[id]) : nullptr;
    }

    template <typename T>
    [[nodiscard]] TRY_INLINE T& Get() const noexcept {
        const uint32_t id = ScopedTypeId<EngineSystemCounter, T>::Value();
        TRY_ASSERT(id < MAX_SYSTEMS, "Max system count limit above");
        TRY_ASSERT(static_cast<T*>(instances_[id]), "System not registered");
        return *static_cast<T*>(instances_[id]);
    }
private:
    void DestroyAt(uint32_t id) noexcept {
        if (instances_[id] == nullptr)
            return;
        TRY_ASSERT(destroyers_[id] != nullptr, "Instance without destructor!");
        destroyers_[id](instances_[id]);
        instances_[id]  = nullptr;
        destroyers_[id] = nullptr;
    }

    void Shutdown() noexcept {
        for (uint32_t i = ScopedTypeCounter<EngineSystemCounter>::counter; i > 0; --i) {
            DestroyAt(i - 1);
        }
    }

    struct EngineSystemCounter; // Чтобы никто из вне не мог сломать индексацию

    void* instances_[MAX_SYSTEMS] = {};
    void (*destroyers_[MAX_SYSTEMS])(void*) = {};
};

} // namespace tryengine::core