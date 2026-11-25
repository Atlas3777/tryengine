#pragma once

#include <EASTL/hash_map.h>
#include <memory>
#include <mutex>

#include "engine/async/GlobalExecutors.hpp"
#include "engine/async/RunAndForget.hpp"
#include "engine/async/Task.hpp"
#include "engine/core/MakeError.hpp"
#include "engine/resources/AssetBytes.hpp"
#include "engine/resources/AssetRegistry.hpp"
#include "engine/resources/AsyncFileManager.hpp"
#include "engine/resources/ResourceControlBlock.hpp"
#include "engine/resources/ResourceHandle.hpp"

namespace tryengine::resources {

class ICacheBase {
public:
    virtual ~ICacheBase() = default;
    virtual void Purge() = 0;
    // Убирает ресурс из кеша, не уничтожая его: старые handles продолжают жить.
    // Нужен редактору для перезагрузки материала после правки файла.
    virtual bool Invalidate(uint64_t guid) = 0;
    virtual void RequestLoad(uint64_t guid) = 0;
    virtual bool IsReady(uint64_t guid) = 0;
    virtual bool IsLoading(uint64_t guid) = 0;
    virtual bool IsFailed(uint64_t guid) = 0;
    virtual void* GetRawPointer(uint64_t guid) = 0;
    virtual bool Release(uint64_t guid) = 0;
    virtual bool HasGuid(uint64_t guid) const = 0;
    //! Снимок guid'ов, лежащих в кеше. Нужен редактору после переимпорта: загруженные
    //! материалы пересоздаются, чтобы подхватить новую текстуру/шейдер.
    virtual void CollectGuids(eastl::vector<uint64_t>& out) const = 0;
};

template <typename T>
class IResourceCache : public ICacheBase {
public:
    virtual async::Task<ResourceHandle<T>> GetOrLoadAsync(uint64_t guid) = 0;
    virtual ResourceHandle<T> Get(uint64_t guid) = 0;
};

template <typename T, typename Loader>
class ResourceCache final : public IResourceCache<T> {
public:
    ResourceCache(AsyncFileManager& fm, AssetRegistry& registry, Loader loader)
        : file_manager_(fm), registry_(registry), loader_(std::move(loader)) {}

    async::Task<ResourceHandle<T>> GetOrLoadAsync(uint64_t guid) override {
        auto [cb, is_first_initiator] = GetOrCreateControlBlock(guid);

        if (!is_first_initiator) {
            if (cb->state.load(std::memory_order_acquire) == ResourceState::Loading)
                co_await WaitForLoadAwaiter{cb};

            if (cb->state.load(std::memory_order_acquire) == ResourceState::Failed)
                co_return LogAndMakeError("Failed to load resource with GUID {}", guid);

            co_return ResourceHandle<T>(cb);
        }

        co_await LoadAsync(guid, cb);

        if (cb->state.load(std::memory_order_acquire) == ResourceState::Failed)
            co_return LogAndMakeError("Failed to load resource with GUID {}", guid);

        co_return ResourceHandle<T>(cb);
    }

    ResourceHandle<T> Get(uint64_t guid) override {
        auto [cb, is_first_initiator] = GetOrCreateControlBlock(guid);

        if (is_first_initiator)
            async::RunAndForget(async::ThreadPool(), LoadAsync(guid, cb));

        return ResourceHandle<T>(cb);
    }

    void RequestLoad(uint64_t guid) override {
        auto [cb, is_first_initiator] = GetOrCreateControlBlock(guid);
        cb->ref_count.fetch_add(1, std::memory_order_relaxed);

        if (is_first_initiator)
            async::RunAndForget(async::ThreadPool(), LoadAsync(guid, cb));
    }

    bool IsReady(uint64_t guid) override {
        std::lock_guard lock(cache_mutex_);
        auto it = cache_.find(guid);
        if (it != cache_.end() && it->second)
            return it->second->state.load(std::memory_order_acquire) == ResourceState::Ready;
        return false;
    }

    bool IsLoading(uint64_t guid) override {
        std::lock_guard lock(cache_mutex_);
        auto it = cache_.find(guid);
        if (it != cache_.end() && it->second)
            return it->second->state.load(std::memory_order_acquire) == ResourceState::Loading;
        return false;
    }

    bool IsFailed(uint64_t guid) override {
        std::lock_guard lock(cache_mutex_);
        auto it = cache_.find(guid);
        if (it != cache_.end() && it->second)
            return it->second->state.load(std::memory_order_acquire) == ResourceState::Failed;
        return false;
    }

    void* GetRawPointer(uint64_t guid) override {
        std::lock_guard lock(cache_mutex_);
        auto it = cache_.find(guid);
        if (it != cache_.end() && it->second) {
            if (it->second->state.load(std::memory_order_acquire) == ResourceState::Ready)
                return static_cast<void*>(it->second->data);
        }
        return nullptr;
    }

    bool Release(uint64_t guid) override {
        std::lock_guard lock(cache_mutex_);
        auto it = cache_.find(guid);
        if (it == cache_.end())
            return false;

        // Вызов только уменьшает счетчик. Очистка произойдет в Purge()
        it->second->ref_count.fetch_sub(1, std::memory_order_acq_rel);
        return true;
    }

    bool HasGuid(uint64_t guid) const override {
        std::lock_guard lock(cache_mutex_);
        if (cache_.find(guid) != cache_.end())
            return true;
        return registry_.GetAssetLocation(guid).has_value();
    }

    void CollectGuids(eastl::vector<uint64_t>& out) const override {
        std::lock_guard lock(cache_mutex_);
        out.reserve(out.size() + cache_.size());
        for (const auto& entry : cache_)
            out.push_back(entry.first);
    }

    // Убирает ресурс из кеша, не уничтожая его: старые handles продолжают жить.
    // Следующий RequestLoad/GetAsync по тому же guid создаст новый ресурс.
    bool Invalidate(uint64_t guid) override {
        std::lock_guard lock(cache_mutex_);
        auto it = cache_.find(guid);
        if (it == cache_.end())
            return false;
        if (it->second->state.load(std::memory_order_acquire) == ResourceState::Loading)
            return false;

        // Не уничтожаем данные: на старый указатель ещё могут ссылаться кадры/хендлы.
        // Ресурс доживает до разрушения кеша, а по тому же guid создаётся новый.
        retired_.push_back(std::move(it->second));
        cache_.erase(it);
        return true;
    }

    void Purge() override {
        std::lock_guard lock(cache_mutex_);
        for (auto it = cache_.begin(); it != cache_.end();) {
            const auto state = it->second->state.load(std::memory_order_acquire);
            const uint32_t refs = it->second->ref_count.load(std::memory_order_acquire);

            // Ресурс уничтожается ТОЛЬКО если на него нет ссылок и он НЕ находится в процессе загрузки
            if (refs == 0 && state != ResourceState::Loading)
                it = cache_.erase(it);
            else
                ++it;
        }
    }

private:
    struct WaitForLoadAwaiter {
        ResourceControlBlock<T>* cb;

        bool await_ready() const noexcept {
            return cb->state.load(std::memory_order_acquire) != ResourceState::Loading;
        }

        void await_suspend(std::coroutine_handle<> awaiting) noexcept {
            std::lock_guard lock(cb->waiters_mutex);
            if (cb->state.load(std::memory_order_acquire) == ResourceState::Loading)
                cb->waiters.push_back(awaiting);
            else
                awaiting.resume();
        }

        void await_resume() const noexcept {}
    };

    std::pair<ResourceControlBlock<T>*, bool> GetOrCreateControlBlock(uint64_t guid) {
        bool is_first_initiator = false;

        std::lock_guard lock(cache_mutex_);
        auto it = cache_.find(guid);
        if (it != cache_.end())
            return {it->second.get(), false};

        auto cb = std::make_unique<ResourceControlBlock<T>>();
        cb->state.store(ResourceState::Loading, std::memory_order_release);

        auto* cb_ptr = cb.get();
        cache_[guid] = std::move(cb);
        is_first_initiator = true;

        return {cb_ptr, is_first_initiator};
    }

    async::Task<void> LoadAsync(uint64_t guid, ResourceControlBlock<T>* cb) {
        const auto asset_location_res = registry_.GetAssetLocation(guid);

        if (!asset_location_res.has_value()) {
            if (registry_.TryDefer(guid,
                                   [this, guid, cb] { async::RunAndForget(async::ThreadPool(), LoadAsync(guid, cb)); }))
                co_return {};  // cb остаётся Loading, waiters ждут, Purge Loading не трогает
            NotifyFailed(cb);
            co_return LogAndMakeError("Path for guid {} not found", guid);
        }

        auto bytes = co_await ReadAssetBytes(file_manager_, registry_, guid);
        if (!bytes.has_value()) {
            NotifyFailed(cb);
            co_return LogAndMakeError("Failed to read asset GUID {}: {}", guid, bytes.error());
        }

        auto parse_result = co_await loader_.Parse(eastl::span<const uint8_t>(bytes->data(), bytes->size()));
        if (!parse_result.has_value()) {
            NotifyFailed(cb);
            co_return LogAndMakeError("Failed to parse asset GUID {}: {}", guid, parse_result.error());
        }

        cb->data = new T(std::move(*parse_result));
        cb->state.store(ResourceState::Ready, std::memory_order_release);

        ResumeWaiters(cb);
        co_return {};
    }

    void NotifyFailed(ResourceControlBlock<T>* cb) {
        cb->state.store(ResourceState::Failed, std::memory_order_release);
        ResumeWaiters(cb);
    }

    void ResumeWaiters(ResourceControlBlock<T>* cb) {
        eastl::vector<std::coroutine_handle<>> waiters_to_resume;
        {
            std::lock_guard lock(cb->waiters_mutex);
            waiters_to_resume = std::move(cb->waiters);
        }
        for (auto handle : waiters_to_resume)
            if (handle)
                handle.resume();
    }

    mutable std::mutex cache_mutex_;
    eastl::hash_map<uint64_t, std::unique_ptr<ResourceControlBlock<T>>> cache_;
    eastl::vector<std::unique_ptr<ResourceControlBlock<T>>> retired_;
    AsyncFileManager& file_manager_;
    AssetRegistry& registry_;
    Loader loader_;
};

}  // namespace tryengine::resources