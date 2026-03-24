#pragma once

#include "ICacheBase.hpp"

namespace engine::resources {
class ResourceManager {
   public:
    template <typename T, typename Loader>
    void RegisterCache(Loader&& loader) {
        auto typeId = entt::type_hash<T>::value();
        m_caches[typeId] = std::make_unique<CacheImpl<T, Loader>>(std::forward<Loader>(loader));
    }

    template <typename T>
    entt::resource<T> Get(entt::id_type id) {
        auto typeId = entt::type_hash<T>::value();
        auto& path = m_assetPaths.at(id);

        auto* typedCache = static_cast<ITypedCache<T>*>(m_caches[typeId].get());
        return typedCache->GetOrLoad(id, path);
    }

    // Вызывать раз в несколько минут или при смене сцены
    void UpdatePurge() {
        for (auto& [id, cache] : m_caches) {
            cache->Purge();
        }
    }

   private:
    std::unordered_map<entt::id_type, std::string> m_assetPaths;
    std::unordered_map<entt::id_type, std::unique_ptr<ICacheBase>> m_caches;
};
}  // namespace engine::resources