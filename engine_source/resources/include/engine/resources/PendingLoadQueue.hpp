#pragma once

#include <EASTL/hash_map.h>
#include <EASTL/vector.h>
#include <mutex>

#include "engine/resources/AssetRegistry.hpp"

namespace tryengine::resources {

class PendingLoadQueue final : public IAssetMissHandler {
public:
    explicit PendingLoadQueue(AssetRegistry& registry) : registry_(registry) { registry_.SetMissHandler(this); }

    bool Defer(uint64_t guid, eastl::function<void()> retry) override {
        {
            std::lock_guard lock(mutex_);
            // перепроверка под тем же локом, что и в OnRegistered: гонки "потерянного пробуждения" нет
            if (!registry_.GetAssetLocation(guid)) {
                pending_[guid].push_back(std::move(retry));
                return true;
            }
        }
        retry();  // успели зарегистрировать между промахом и Defer
        return true;
    }

    void OnRegistered(uint64_t guid) override {
        eastl::vector<eastl::function<void()>> ready;
        {
            std::lock_guard lock(mutex_);
            auto it = pending_.find(guid);
            if (it == pending_.end())
                return;
            ready = std::move(it->second);
            pending_.erase(it);
        }
        for (auto& f : ready)
            f();
    }

private:
    AssetRegistry& registry_;
    std::mutex mutex_;
    eastl::hash_map<uint64_t, eastl::vector<eastl::function<void()>>> pending_;
};

}  // namespace tryeditor