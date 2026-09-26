#pragma once

#include <EASTL/hash_map.h>
#include <EASTL/optional.h>
#include <mutex>

#include "engine/resources/AssetLocation.hpp"

namespace tryengine::resources {

class AssetRegistry {
public:
    void RegisterLooseArtifact(uint64_t guid, eastl::string path) {
        LogTrace("Asset {} registred", guid);
        std::lock_guard lock(mutex_);
        locations_[guid] = LooseFileLocation{std::move(path)};
    }

    void RegisterPakArtifact(uint64_t guid, const PakChunkLocation& location) {
        LogTrace("Asset {} registred", guid);
        std::lock_guard lock(mutex_);
        locations_[guid] = location;
    }

    eastl::optional<AssetLocation> GetAssetLocation(uint64_t guid) const {
        std::lock_guard lock(mutex_);
        auto it = locations_.find(guid);
        if (it != locations_.end()) {
            return it->second;
        }
        return eastl::nullopt;
    }

private:
    mutable std::mutex mutex_;
    eastl::hash_map<uint64_t, AssetLocation> locations_;
};

}  // namespace tryengine::resources