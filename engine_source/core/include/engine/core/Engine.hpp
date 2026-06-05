#pragma once

#include <cassert>
#include <memory>
#include <unordered_map>
#include <utility>

namespace tryengine::core {

using TypeId = uint32_t;

class TypeRegistry {
    static TypeId NextId() noexcept {
        static TypeId counter = 0;
        return counter++;
    }

public:
    template <typename T>
    static TypeId GetId() noexcept {
        static const TypeId id = NextId();
        return id;
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

        auto system = std::make_shared<T>(std::forward<Args>(args)...);
        systems_[typeId] = system;

        // Возвращаем ссылку. Безопасно, так как объект уже жив в куче
        return *system;
    }

    template <typename T>
    T& RegisterSystem(std::shared_ptr<T> system) {
        auto typeId = TypeRegistry::GetId<T>();

        // Кастуем к shared_ptr<void> для хранения в мапе
        systems_[typeId] = std::static_pointer_cast<void>(system);

        return *system;
    }

    template <typename T>
    [[nodiscard]] T& Get() const {
        auto* system = FindInternal<T>();
        assert(system != nullptr && "System not found!");
        return *system;
    }

    // 2. Для опциональных систем (может не быть)
    template <typename T>
    [[nodiscard]] T* TryGet() const {
        return FindInternal<T>();
    }

private:
    template <typename T>
    T* FindInternal() const {
        auto typeId = TypeRegistry::GetId<T>();
        auto it = systems_.find(typeId);
        if (it != systems_.end()) {
            return static_cast<T*>(it->second.get());
        }
        return nullptr;
    }

    std::unordered_map<TypeId, std::shared_ptr<void>> systems_;
};

}  // namespace tryengine::core