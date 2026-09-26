#pragma once

#include <EASTL/shared_ptr.h>
#include <EASTL/vector.h>

#include "engine/core/Inline.hpp"
#include "engine/core/Assert.hpp"
#include "engine/core/TypeRegistry.hpp"

namespace tryengine::core {

class Engine {
public:
    Engine() = default;
    ~Engine() {
        LogInfo("Engine destructor start");
        while (!systems_.empty()) {
            systems_.pop_back();
        }
        LogInfo("Engine destructor end");
    }

    template <typename T, typename... Args>
    T& RegisterSystem(Args&&... args) {
        auto type_id = ScopedTypeId<Engine, T>::Value();

        if (type_id >= systems_.size())
            systems_.resize(type_id + 1);

        auto system = eastl::make_shared<T>(std::forward<Args>(args)...);
        systems_[type_id] = system;

        return *system;
    }

    template <typename T>
    __forceinline [[nodiscard]] T& Get() const noexcept {
        auto* system = FindInternal<T>();
        TRY_ASSERT(system, "System not found!");
        return *system;
    }

    template <typename T>
    __forceinline [[nodiscard]] T* TryGet() const noexcept {
        return FindInternal<T>();
    }

private:
    template <typename T>
    T* FindInternal() const noexcept {
        auto type_id = ScopedTypeId<Engine, T>::Value();
        if (type_id < systems_.size())
            return static_cast<T*>(systems_[type_id].get());

        return nullptr;
    }

    eastl::vector<eastl::shared_ptr<void>> systems_;
};

}  // namespace tryengine::core