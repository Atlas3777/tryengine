#pragma once

#include <entt/core/type_info.hpp>

#include "engine/core/ICacheBase.hpp"
#include "engine/core/AssetDatabase.hpp"

namespace engine::core {
class ResourceManager {
   public:
    ResourceManager() {
        asset_database_ = std::make_unique<AssetDatabase>();
    };

    template <typename T, typename Loader>
    void RegisterLoader(Loader&& loader) {
        auto typeId = entt::type_hash<T>::value();
        caches_[typeId] = std::make_unique<CacheImpl<T, Loader>>(std::forward<Loader>(loader));
    }

    template <typename T>
    entt::resource<T> Get(uint64_t id) {
        auto typeId = entt::type_hash<T>::value();
        auto path = asset_database_->GetPath(id);

        auto* typedCache = static_cast<ITypedCache<T>*>(caches_[typeId].get());
        return typedCache->GetOrLoad(id, id, path);
    }

    // Вызывать раз в несколько минут или при смене сцены
    // TODO: доделать
    void UpdatePurge() {
        for (auto& [id, cache] : caches_) {
            cache->Purge();
        }
    }

    AssetDatabase& GetAssetDatabase() const { return *asset_database_;};

   private:
    std::unique_ptr<AssetDatabase> asset_database_;
    std::unordered_map<entt::id_type, std::unique_ptr<ICacheBase>> caches_;
};
}  // namespace engine::resources