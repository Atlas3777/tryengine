#pragma once

#include <EASTL/hash_map.h>
#include <EASTL/optional.h>
#include <mutex>

#include "engine/resources/AssetLocation.hpp"

namespace tryengine::resources {

// Runtime тип, который используется в билде, в отличие от asset pipeline
class AssetRegistry {
public:
    void RegisterLooseArtifact(uint64_t guid, eastl::string path) {
        // LogTrace("Loose asset {} registred, {}", guid, path);
        std::lock_guard lock(mutex_);
        locations_[guid] = LooseFileLocation{std::move(path)};
    }

    void RegisterPakArtifact(uint64_t guid, const PakChunkLocation& location) {
        // LogTrace("Pak asset {} registred", guid);
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