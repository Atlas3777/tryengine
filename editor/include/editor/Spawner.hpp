#pragma once

#include "engine/async/Task.hpp"
#include "entt/entity/fwd.hpp"

namespace tryeditor {
class AssetSourceDatabase;

class Spawner {
public:
    Spawner(AssetSourceDatabase& asset_source_db)
        :  asset_source_db_(asset_source_db){};

    tryengine::async::Task<void> Spawn(entt::registry& reg, uint64_t asset_id) const;

private:
    AssetSourceDatabase& asset_source_db_;
};

}  // namespace tryeditor
