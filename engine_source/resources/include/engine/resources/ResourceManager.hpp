#pragma once

#include <EASTL/vector.h>
#include <memory>
#include <mutex>

#include "engine/async/Task.hpp"
#include "engine/core/MakeError.hpp"
#include "engine/core/TypeRegistry.hpp"
#include "engine/resources/AsyncFileManager.hpp"
#include "engine/resources/ResourceCache.hpp"

namespace tryengine::resources {

class ResourceManager {
public:
    ResourceManager(AsyncFileManager& file_manager, AssetRegistry& registry)
        : file_manager_(file_manager), registry_(registry){}

    template <typename T, typename Loader>
    void RegisterType(Loader&& loader) {
        RegisterTypeWithLocator<T>(std::forward<Loader>(loader), AssetRegistryLocator{registry_});
    }

    template <typename T, typename Loader, typename Locator>
    void RegisterTypeWithLocator(Loader&& loader, Locator&& locator) {
        auto type_id = core::ScopedTypeId<ResourceManager, T>::Value();

        std::lock_guard lock(mutex_);

        if (type_id >= caches_.size())
            caches_.resize(type_id + 1);

        caches_[type_id] = std::make_unique<ResourceCache<T, std::decay_t<Loader>, std::decay_t<Locator>>>(
            file_manager_, std::forward<Loader>(loader), std::forward<Locator>(locator));
    }

    template <typename T>
    async::Task<ResourceHandle<T>> GetAsync(uint64_t guid) {
        ICacheBase* cache_ptr = nullptr;
        {
            std::lock_guard lock(mutex_);
            auto type_id = core::ScopedTypeId<ResourceManager, T>::Value();
            if (type_id < caches_.size()) {
                cache_ptr = caches_[type_id].get();
            }
        }

        if (!cache_ptr) {
            co_return LogAndMakeError("Loader Not Registered in ResourceManager guid {}", guid);
        }

        auto* typed_cache = static_cast<IResourceCache<T>*>(cache_ptr);

        co_return co_await typed_cache->GetOrLoadAsync(guid);
    }

    template <typename T>
    Result<ResourceHandle<T>> Get(uint64_t guid) {
        ICacheBase* cache_ptr = nullptr;
        {
            std::lock_guard lock(mutex_);
            auto type_id = core::ScopedTypeId<ResourceManager, T>::Value();
            if (type_id < caches_.size()) {
                cache_ptr = caches_[type_id].get();
            }
        }

        if (!cache_ptr)
            return LogAndMakeError("Loader Not Registered in ResourceManager guid {}", guid);

        auto* typed_cache = static_cast<IResourceCache<T>*>(cache_ptr);
        return typed_cache->Get(guid);
    }

    template <typename T>
    ResourceHandle<T> GetUnsafe(uint64_t guid) {
        ICacheBase* cache_ptr = nullptr;
        {
            std::lock_guard lock(mutex_);
            auto type_id = core::ScopedTypeId<ResourceManager, T>::Value();
            if (type_id < caches_.size()) {
                cache_ptr = caches_[type_id].get();
            }
        }
        auto* typed_cache = static_cast<IResourceCache<T>*>(cache_ptr);
        return typed_cache->Get(guid);
    }

    void Purge() {
        std::lock_guard lock(mutex_);
        for (auto& cache : caches_) {
            if (cache) {
                cache->Purge();
            }
        }
    }

    AssetRegistry& GetAssetRegistry() { return registry_; }

private:
    std::mutex mutex_;
    AsyncFileManager& file_manager_;
    AssetRegistry& registry_;
    eastl::vector<std::unique_ptr<ICacheBase>> caches_;
};

}  // namespace tryengine::resources