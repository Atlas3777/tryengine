#pragma once

#include <EASTL/shared_ptr.h>
#include <EASTL/vector.h>

#include "Inline.hpp"
#include "engine/core/Assert.hpp"
#include "engine/core/TypeRegistry.hpp"

namespace tryengine::core {

class Engine {
public:
    Engine() = default;
    ~Engine() = default;

    template <typename T, typename... Args>
    T& RegisterSystem(Args&&... args) {
        auto typeId = ScopedTypeId<Engine, T>::Value();

        if (typeId >= systems_.size())
            systems_.resize(typeId + 1);

        auto system = eastl::make_shared<T>(std::forward<Args>(args)...);
        systems_[typeId] = system;

        return *system;
    }

    template <typename T>
    __forceinline [[nodiscard]] T& Get() const {
        auto* system = FindInternal<T>();
        TRY_ASSERT(system, "System not found!");
        return *system;
    }

    template <typename T>
    __forceinline [[nodiscard]] T* TryGet() const {
        return FindInternal<T>();
    }

private:
    template <typename T>
    __forceinline T* FindInternal() const {
        auto typeId = ScopedTypeId<Engine, T>::Value();
        if (typeId < systems_.size()) {
            return static_cast<T*>(systems_[typeId].get());
        }
        return nullptr;
    }

    eastl::vector<eastl::shared_ptr<void>> systems_;
};

}  // namespace tryengine::core