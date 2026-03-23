#pragma once
#include <entt/resource/cache.hpp>
#include <memory>
#include <string>
#include <unordered_map>

namespace engine::resources {

// 1. Базовый нешаблонный класс, чтобы хранить всё в одном map
class ICacheBase {
   public:
    virtual ~ICacheBase() = default;
};

// 2. Шаблонный интерфейс, который знает про тип T, но НЕ ЗНАЕТ про лоадер
template <typename T>
class ITypedCache : public ICacheBase {
   public:
    // Возвращает хэндл. Если нет в кэше — загрузит.
    virtual entt::resource<T> GetOrLoad(entt::id_type id, const std::string& path) = 0;
};

// Вспомогательный лоадер-заглушка для EnTT
template <typename T>
struct DirectLoader {
    using result_type = std::shared_ptr<T>;

    // EnTT вызовет этот оператор и передаст в него наш готовый resourceObj
    result_type operator()(std::shared_ptr<T> ptr) const {
        return ptr;
    }
};

template <typename T, typename Loader>
class CacheImpl : public ITypedCache<T> {
private:
    // ВАЖНО: указываем DirectLoader вторым аргументом шаблона
    entt::resource_cache<T, DirectLoader<T>> m_cache;
    Loader m_loader;

public:
    explicit CacheImpl(Loader loader) : m_loader(std::move(loader)) {}

    entt::resource<T> GetOrLoad(entt::id_type id, const std::string& path) override {
        if (!m_cache.contains(id)) {
            // 1. Твой GpuMeshLoader делает реальную работу
            std::shared_ptr<T> resourceObj = m_loader(path);

            // 2. Теперь вызываем load БЕЗ <...>.
            // Он просто пробросит resourceObj в DirectLoader::operator()
            m_cache.load(id, std::move(resourceObj));
        }

        // 3. Возвращаем handle (в этой версии через operator[])
        return m_cache[id];
    }
};
}  // namespace engine::resource