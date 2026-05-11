#pragma once

#include "engine/core/ResourceManager.hpp"
#include "engine/graphics/GraphicsContext.hpp"
#include "entt/entity/fwd.hpp"
#include "import/ImportSystem.hpp"

namespace tryeditor {
class Spawner {
public:
    Spawner(tryengine::graphics::GraphicsContext& graphics_context, tryengine::core::ResourceManager& resource_manager,
            ImportSystem& import_system)
        : resource_manager_(resource_manager), graphics_context_(graphics_context), import_system_(import_system) {};

    void Spawn(entt::registry& reg, uint64_t asset_id) const;

private:
    tryengine::core::ResourceManager& resource_manager_;
    tryengine::graphics::GraphicsContext& graphics_context_;
    ImportSystem& import_system_;
};

}  // namespace tryeditor
