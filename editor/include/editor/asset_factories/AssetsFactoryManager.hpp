#pragma once
#include <filesystem>
#include <iostream>
#include <memory>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "editor/asset_factories/IAssetFactory.hpp"

namespace tryeditor {

class AssetsFactoryManager {
public:
    AssetsFactoryManager() = default;

    // Регистрация фабрики
    template <typename T, typename... Args>
    void RegisterFactory(Args&&... args) {
        static_assert(std::is_base_of_v<IAssetFactory, T>, "T must derive from IAssetFactory");

        auto factory = std::make_unique<T>(std::forward<Args>(args)...);
        std::string action_name = factory->GetActionName();

        std::cout << "Registering factory for: " << action_name << std::endl;

        gui_factories_.push_back(factory.get());
        type_map_[std::type_index(typeid(T))] = std::move(factory);
    }

    // Возвращает список всех фабрик для отрисовки в GUI меню
    [[nodiscard]] const std::vector<IAssetFactory*>& GetFactories() const { return gui_factories_; }

    // Вызов создания из кода с пробросом кастомных параметров
    // Пример использования: manager.Create<ShaderAssetFactory>(path, "MyShader", myShaderDef);
    template <typename TFactory, typename... Args>
    uint64_t Create(const std::filesystem::path& path, Args&&... args) {
        auto it = type_map_.find(std::type_index(typeid(TFactory)));
        if (it != type_map_.end()) {
            // Безопасный каст, так как мы сами зарегистрировали этот тип
            auto* factory = static_cast<TFactory*>(it->second.get());
            // Вызываем специфичный метод Create конкретной фабрики
            return factory->Create(path, std::forward<Args>(args)...);
        }

        std::cerr << "Factory not found for type: " << typeid(TFactory).name() << std::endl;
        return 0;  // Или бросить исключение
    }

private:
    std::unordered_map<std::type_index, std::unique_ptr<IAssetFactory>> type_map_;
    std::vector<IAssetFactory*> gui_factories_;
    const std::filesystem::path asset_dir = "game/assets";
};

}  // namespace tryeditor
