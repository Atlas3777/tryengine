#pragma once

#include <EASTL/hash_map.h>
#include <mutex>

#include "engine/async/GlobalExecutors.hpp"
#include "engine/async/RunAndForget.hpp"
#include "engine/async/Task.hpp"
#include "engine/core/MakeError.hpp"
#include "engine/resources/AssetRegistry.hpp"
#include "engine/resources/AsyncFileManager.hpp"
#include "engine/resources/ResourceControlBlock.hpp"
#include "engine/resources/ResourceHandle.hpp"

namespace tryengine::resources {

class ICacheBase {
public:
    virtual ~ICacheBase() = default;
    virtual void Purge() = 0;
};

template <typename T>
class IResourceCache : public ICacheBase {
public:
    virtual async::Task<ResourceHandle<T>> GetOrLoadAsync(uint64_t guid) = 0;
    virtual ResourceHandle<T> Get(uint64_t guid) = 0;
};

struct AssetRegistryLocator {
    AssetRegistry& registry;
    eastl::optional<eastl::string> operator()(uint64_t guid) const { return registry.GetArtifactPath(guid); }
};

template <typename T, typename Loader, typename Locator = AssetRegistryLocator>
class ResourceCache final : public IResourceCache<T> {
public:
    ResourceCache(AsyncFileManager& fm, Loader loader, Locator locator)
        : file_manager_(fm), loader_(std::move(loader)), locator_(std::move(locator)) {}

    async::Task<ResourceHandle<T>> GetOrLoadAsync(uint64_t guid) override {
        auto [cb, is_first_initiator] = GetOrCreateControlBlock(guid);

        if (!is_first_initiator) {
            if (cb->state.load(std::memory_order_acquire) == ResourceState::Loading) {
                co_await WaitForLoadAwaiter{cb};
            }

            if (cb->state.load(std::memory_order_acquire) == ResourceState::Failed) {
                co_return LogAndMakeError("Failed to load resource with GUID {}", guid);
            }

            co_return ResourceHandle<T>(cb);
        }

        co_await LoadAsync(guid, cb);

        if (cb->state.load(std::memory_order_acquire) == ResourceState::Failed) {
            co_return LogAndMakeError("Failed to load resource with GUID {}", guid);
        }

        co_return ResourceHandle<T>(cb);
    }

    ResourceHandle<T> Get(uint64_t guid) override {
        auto [cb, is_first_initiator] = GetOrCreateControlBlock(guid);

        if (is_first_initiator) {
            async::RunAndForget(async::ThreadPool(), LoadAsync(guid, cb));
        }

        return ResourceHandle<T>(cb);
    }

    void Purge() override {
        std::lock_guard lock(cache_mutex_);
        for (auto it = cache_.begin(); it != cache_.end();) {
            if (it->second.expired()) {
                it = cache_.erase(it);
            } else {
                ++it;
            }
        }
    }

private:
    struct WaitForLoadAwaiter {
        std::shared_ptr<ResourceControlBlock<T>> cb;

        bool await_ready() const noexcept {
            return cb->state.load(std::memory_order_acquire) != ResourceState::Loading;
        }

        void await_suspend(std::coroutine_handle<> awaiting) noexcept {
            std::lock_guard lock(cb->waiters_mutex);
            if (cb->state.load(std::memory_order_acquire) == ResourceState::Loading) {
                cb->waiters.push_back(awaiting);
            } else {
                awaiting.resume();
            }
        }

        void await_resume() const noexcept {}
    };

    std::pair<std::shared_ptr<ResourceControlBlock<T>>, bool> GetOrCreateControlBlock(uint64_t guid) {
        std::shared_ptr<ResourceControlBlock<T>> cb;
        bool is_first_initiator = false;

        std::lock_guard lock(cache_mutex_);
        auto it = cache_.find(guid);
        if (it != cache_.end()) {
            cb = it->second.lock();
        }

        if (!cb) {
            cb = std::make_shared<ResourceControlBlock<T>>();
            cb->state.store(ResourceState::Loading, std::memory_order_release);
            cache_[guid] = cb;
            is_first_initiator = true;
        }

        return {cb, is_first_initiator};
    }

    // Собственно загрузка: stat -> read -> parse. Не зависит от того, ждёт
    // ли кто-то результат синхронно (GetOrLoadAsync) или это fire-and-forget (Get).
    async::Task<void> LoadAsync(uint64_t guid, std::shared_ptr<ResourceControlBlock<T>> cb) {
        const auto artifact_path_res= locator_(guid);

        if (!artifact_path_res.has_value())
            co_return LogAndMakeError("Path for guid {} not found", guid);


        auto artifact_path = *artifact_path_res;

        FileHandle stat_handle = file_manager_.GetStatAsync(artifact_path);
        co_await stat_handle;

        if (stat_handle.IsFailed()) {
            NotifyFailed(cb);
            co_return LogAndMakeError("Failed to stat artifact path: '{}'", artifact_path);
        }

        FileHandle read_handle = file_manager_.ReadChunkAsync(artifact_path, 0, stat_handle.GetFileSize());
        file_manager_.Submit();
        co_await read_handle;

        if (read_handle.IsFailed()) {
            NotifyFailed(cb);
            co_return LogAndMakeError("Failed to read artifact: {}", artifact_path);
        }

        auto parse_result = co_await loader_.Parse(read_handle.GetData());

        if (!parse_result.has_value()) {
            NotifyFailed(cb);
            co_return LogAndMakeError("Failed to parse asset GUID {}: {}", guid, parse_result.error());
        }

        cb->data = std::make_unique<T>(std::move(*parse_result));
        cb->state.store(ResourceState::Ready, std::memory_order_release);

        ResumeWaiters(cb);
        co_return {};
    }

    void NotifyFailed(std::shared_ptr<ResourceControlBlock<T>>& cb) {
        cb->state.store(ResourceState::Failed, std::memory_order_release);
        ResumeWaiters(cb);
    }

    void ResumeWaiters(std::shared_ptr<ResourceControlBlock<T>>& cb) {
        eastl::vector<std::coroutine_handle<>> waiters_to_resume;
        {
            std::lock_guard lock(cb->waiters_mutex);
            waiters_to_resume = std::move(cb->waiters);
        }
        for (auto handle : waiters_to_resume) {
            if (handle)
                handle.resume();
        }
    }

    std::mutex cache_mutex_;
    eastl::hash_map<uint64_t, std::weak_ptr<ResourceControlBlock<T>>> cache_;
    AsyncFileManager& file_manager_;
    Locator locator_;
    Loader loader_;
};

}  // namespace tryengine::resources