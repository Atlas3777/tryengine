#pragma once

#include <EASTL/unordered_map.h>

#include "editor/asset_factories/IAssetFactory.hpp"

namespace tryeditor {

class AssetsFactoryManager {
public:
    template <typename TFactory, typename... Args>
    void RegisterFactory(Args&&... args) {
        auto factory = std::make_unique<TFactory>(std::forward<Args>(args)...);

        eastl::string type_name = factory->GetAssetType();
        auto type_id = tryengine::core::ScopedTypeId<AssetsFactoryManager, TFactory>::value();

        gui_factories_.push_back(factory.get());
        factories_by_name_[type_name] = factory.get();
        storage_[type_id] = std::move(factory);
    }

    IAssetFactory* GetFactoryByName(const eastl::string& name) {
        auto it = factories_by_name_.find(name);
        return (it != factories_by_name_.end()) ? it->second : nullptr;
    }

    template <typename TFactory>
    TFactory* GetFactory() {
        auto type_id = tryengine::core::ScopedTypeId<AssetsFactoryManager, TFactory>::value();

        return static_cast<TFactory*>(storage_[type_id].get());
    }

    const eastl::vector<IAssetFactory*>& GetFactories() const { return gui_factories_; }

private:
    eastl::vector<std::unique_ptr<IAssetFactory>> storage_;
    eastl::unordered_map<eastl::string, IAssetFactory*> factories_by_name_;
    eastl::vector<IAssetFactory*> gui_factories_;
};

}  // namespace tryeditor