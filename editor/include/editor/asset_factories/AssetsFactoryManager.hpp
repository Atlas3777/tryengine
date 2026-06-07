#pragma once

#include <entt/core/type_info.hpp>
#include <memory>
#include <string>
#include <unordered_map>

#include "editor/asset_factories/IAssetFactory.hpp"

namespace tryeditor {

class AssetsFactoryManager {
public:
    template <typename TFactory, typename... Args>
    void RegisterFactory(Args&&... args) {
        auto factory = std::make_unique<TFactory>(std::forward<Args>(args)...);

        std::string type_name = factory->GetAssetType();
        auto type_id = entt::type_hash<TFactory>::value();

        gui_factories_.push_back(factory.get());
        factories_by_name_[type_name] = factory.get();
        storage_[type_id] = std::move(factory);
    }

    IAssetFactory* GetFactoryByName(const std::string& name) {
        auto it = factories_by_name_.find(name);
        return (it != factories_by_name_.end()) ? it->second : nullptr;
    }

    template <typename TFactory>
    TFactory* GetFactory() {
        auto type_id = entt::type_hash<TFactory>::value();
        auto it = storage_.find(type_id);
        if (it != storage_.end()) {
            // Безопасно кастуем базовый указатель к конкретному типу фабрики
            return static_cast<TFactory*>(it->second.get());
        }
        return nullptr;
    }

    const std::vector<IAssetFactory*>& GetFactories() const { return gui_factories_; }

private:
    std::unordered_map<entt::id_type, std::unique_ptr<IAssetFactory>> storage_;
    std::unordered_map<std::string, IAssetFactory*> factories_by_name_;
    std::vector<IAssetFactory*> gui_factories_;
};

}  // namespace tryeditor