#pragma once

#include <EASTL/hash_map.h>
#include <EASTL/optional.h>
#include <mutex>

#include "engine/resources/AssetLocation.hpp"

namespace tryengine::resources {

class IAssetMissHandler {
public:
    virtual ~IAssetMissHandler() = default;
    virtual bool Defer(uint64_t guid, eastl::function<void()> retry) = 0;
    virtual void OnRegistered(uint64_t guid) = 0;
};

// Runtime тип, который используется в билде, в отличие от asset pipeline
class AssetRegistry {
public:
    void RegisterLooseArtifact(uint64_t guid, eastl::string path) {
        // LogTrace("Loose asset {} registred, {}", guid, path);
        {
            std::lock_guard lock(mutex_);
            locations_[guid] = LooseFileLocation{std::move(path)};
        }
        if (miss_handler_)
            miss_handler_->OnRegistered(guid);
    }

    void RegisterPakArtifact(uint64_t guid, const PakChunkLocation& location) {
        // LogTrace("Pak asset {} registred", guid);
        {
            std::lock_guard lock(mutex_);
            locations_[guid] = location;
        }
        if (miss_handler_)
            miss_handler_->OnRegistered(guid);
    }

    eastl::optional<AssetLocation> GetAssetLocation(uint64_t guid) const {
        std::lock_guard lock(mutex_);
        auto it = locations_.find(guid);
        if (it != locations_.end()) {
            return it->second;
        }
        return eastl::nullopt;
    }

    // Вызывать до старта потоков. В game не вызывается.
    void SetMissHandler(IAssetMissHandler* h) { miss_handler_ = h; }
    bool TryDefer(uint64_t guid, eastl::function<void()> retry) {
        return miss_handler_ && miss_handler_->Defer(guid, std::move(retry));
    }

private:
    mutable std::mutex mutex_;
    eastl::hash_map<uint64_t, AssetLocation> locations_;
    IAssetMissHandler* miss_handler_ = nullptr;
};

}  // namespace tryengine::resources