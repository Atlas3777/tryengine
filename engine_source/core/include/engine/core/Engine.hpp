#pragma once

#include <EASTL/shared_ptr.h>
#include <EASTL/vector.h>

#include "engine/core/Assert.hpp"

namespace tryengine::core {

using TypeId = uint32_t;

class TypeRegistry {
public:
    template <typename T>
    static TypeId GetId() noexcept {
        static const TypeId id = NextId();
        return id;
    }

private:
    static TypeId NextId() noexcept {
        static TypeId counter = 0;
        return counter++;
    }
};

}  // namespace tryengine::core

namespace tryengine::core {

class Engine {
public:
    Engine() = default;
    ~Engine() = default;

    template <typename T, typename... Args>
    T& RegisterSystem(Args&&... args) {
        auto typeId = TypeRegistry::GetId<T>();

        if (typeId >= systems_.size()) {
            systems_.resize(typeId + 1);
        }

        auto system = eastl::make_shared<T>(std::forward<Args>(args)...);
        systems_[typeId] = system;

        return *system;
    }

    template <typename T>
    T& RegisterSystem(eastl::shared_ptr<T> system) {
        auto typeId = TypeRegistry::GetId<T>();

        if (typeId >= systems_.size()) {
            systems_.resize(typeId + 1);
        }

        systems_[typeId] = eastl::static_pointer_cast<void>(system);
        return *system;
    }

    template <typename T>
    [[nodiscard]] T& Get() const {
        auto* system = FindInternal<T>();
        TRY_ASSERT(system, "System not found!");
        return *system;
    }

    template <typename T>
    [[nodiscard]] T* TryGet() const {
        return FindInternal<T>();
    }

private:
    template <typename T>
    T* FindInternal() const {
        auto typeId = TypeRegistry::GetId<T>();
        if (typeId < systems_.size()) {
            return static_cast<T*>(systems_[typeId].get());
        }
        return nullptr;
    }

    eastl::vector<eastl::shared_ptr<void>> systems_;
};

}  // namespace tryengine::core