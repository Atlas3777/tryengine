#pragma once
#include <entt/core/type_info.hpp>

#include "ICacheBase.hpp"
#include "engine/graphics/Types.hpp"

namespace engine::resources {

class ResourceManager {
public:
    // --- РЕГИСТРАЦИЯ ---

    // Регистрируем соответствие ID и файла (например, читая manifest.json при старте игры)
    void RegisterAssetPath(entt::id_type id, const std::string& path) {
        m_assetPaths[id] = path;
    }

    // Регистрируем кэш и его загрузчик (Точка сбора!)
    template<typename T, typename Loader>
    void RegisterCache(Loader loader) {
        auto typeId = entt::type_hash<T>::value();
        m_caches[typeId] = std::make_unique<CacheImpl<T, Loader>>(std::move(loader));
    }

    // --- ИСПОЛЬЗОВАНИЕ ---

    // Тот самый идеальный метод, который ты хотел
    template<typename T>
    entt::resource<T> Get(entt::id_type id) {
        auto typeId = entt::type_hash<T>::value();

        // Достаем типизированный кэш
        auto* typedCache = static_cast<ITypedCache<T>*>(m_caches[typeId].get());

        // Достаем путь к файлу из базы ассетов
        const std::string& path = m_assetPaths.at(id);

        // Вся грязная работа спрятана внутри!
        return typedCache->GetOrLoad(id, path);
    }

private:
    std::unordered_map<entt::id_type, std::string> m_assetPaths;
    std::unordered_map<entt::id_type, std::unique_ptr<ICacheBase>> m_caches;
};

}