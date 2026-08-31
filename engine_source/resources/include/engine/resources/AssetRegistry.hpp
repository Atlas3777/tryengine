#pragma once

#include <EASTL/hash_map.h>
#include <EASTL/string.h>
#include <EASTL/optional.h>
#include <mutex>

namespace tryengine::resources {

class AssetRegistry {
public:
    void RegisterArtifact(uint64_t guid, eastl::string artifact_path) {
        std::lock_guard lock(mutex_);
        map_[guid] = std::move(artifact_path);
    }

    eastl::optional<eastl::string> GetArtifactPath(uint64_t guid) const {
        std::lock_guard lock(mutex_);
        auto it = map_.find(guid);
        if (it != map_.end())
            return it->second;

        return eastl::nullopt;
    }

private:
    mutable std::mutex mutex_;
    eastl::hash_map<uint64_t, eastl::string> map_;
};

}  // namespace tryengine::resources