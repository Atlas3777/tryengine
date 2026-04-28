#pragma once
#include <entt/resource/cache.hpp>
#include <entt/resource/resource.hpp>
#include <memory>
#include <string>
#include <unordered_map>

namespace tryengine::core {
class ICacheBase {
   public:
    virtual ~ICacheBase() = default;
    virtual void Purge() = 0;  // Метод для очистки неиспользуемых ресурсов
};

// 2. Шаблонный интерфейс, который знает про тип T, но НЕ ЗНАЕТ про лоадер
template <typename T>
class ITypedCache : public ICacheBase {
   public:
    // Возвращает хэндл. Если нет в кэше — загрузит.
    virtual entt::resource<T> GetOrLoad(uint64_t id, uint64_t id2, const std::string& path) = 0;
};

template <typename T, typename Loader>
class CacheImpl : public ITypedCache<T> {
    // Теперь используем реальный Лоадер как часть типа кэша
    entt::resource_cache<T, Loader> cache_;

   public:
    // Конструируем кэш, передавая ему настроенный лоадер
    explicit CacheImpl(Loader&& loader) : cache_(std::move(loader)) {}

    entt::resource<T> GetOrLoad(uint64_t id,uint64_t id2, const std::string& path) override {
        // .first — это итератор на ресурс, .second — флаг "был ли загружен"
        return cache_.load(id,id, path).first->second;
    }

    //TODO: подумать над улучшениями
    void Purge() override {
        for (auto it = cache_.begin(); it != cache_.end();) {
            // .handle() возвращает ссылку на внутренний std::shared_ptr
            if (it->second.handle().use_count() <= 1) {
                it = cache_.erase(it);
            } else {
                ++it;
            }
        }
    }
};
}  // namespace tryengine::resources