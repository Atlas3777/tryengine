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
        : file_manager_(file_manager), registry_(registry) {}

    ~ResourceManager() {
        LogInfo("Resource manager destructor");
    };

    template <typename T, typename Loader>
    void RegisterType(Loader&& loader) {
        auto type_id = core::ScopedTypeId<ResourceManager, T>::Value();

        std::lock_guard lock(mutex_);

        if (type_id >= caches_.size())
            caches_.resize(type_id + 1);

        caches_[type_id] = std::make_unique<ResourceCache<T, std::decay_t<Loader>>>(
            file_manager_, registry_, std::forward<Loader>(loader));
    }

    template <typename T>
    async::Task<ResourceHandle<T>> GetAsync(uint64_t guid) {
        auto* cache = GetTypedCache<T>();
        if (!cache)
            co_return LogAndMakeError("Loader Not Registered in ResourceManager guid {}", guid);

        co_return co_await cache->GetOrLoadAsync(guid);
    }

    template <typename T>
    Result<ResourceHandle<T>> Get(uint64_t guid) {
        auto* cache = GetTypedCache<T>();
        if (!cache)
            return LogAndMakeError("Loader Not Registered in ResourceManager guid {}", guid);

        return cache->Get(guid);
    }

    // --- daslang / ecs API ---

    template <typename T>
    void RequestLoad(uint64_t guid) {
        auto* cache = GetTypedCache<T>();
        TRY_ASSERT(cache, "Cache for type not registered");
        cache->RequestLoad(guid);
    }

    template <typename T>
    bool IsReady(uint64_t guid) {
        auto* cache = GetTypedCache<T>();
        return cache ? cache->IsReady(guid) : false;
    }

    template <typename T>
    uint64_t GetPointer(uint64_t guid) {
        auto* cache = GetTypedCache<T>();
        TRY_ASSERT(cache, "Cache for type not registered");

        void* raw_ptr = cache->GetRawPointer(guid);
        return reinterpret_cast<uint64_t>(raw_ptr);
    }

    template <typename T>
    void Release(uint64_t guid) {
        auto* cache = GetTypedCache<T>();
        if (cache) {
            cache->Release(guid);
        }
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
    template <typename T>
    IResourceCache<T>* GetTypedCache() {
        std::lock_guard lock(mutex_);
        auto type_id = core::ScopedTypeId<ResourceManager, T>::Value();
        if (type_id < caches_.size()) {
            return static_cast<IResourceCache<T>*>(caches_[type_id].get());
        }
        return nullptr;
    }

    std::mutex mutex_;
    AsyncFileManager& file_manager_;
    AssetRegistry& registry_;
    eastl::vector<std::unique_ptr<ICacheBase>> caches_;
};

}  // namespace tryengine::resources