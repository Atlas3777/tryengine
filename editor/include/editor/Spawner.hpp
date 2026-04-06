#pragma once
#include <cereal/archives/json.hpp>
#include <fstream>

#include "editor/meta/ModelAssetMap.hpp"
#include "engine/core/ResourceManager.hpp"
#include "engine/graphics/GraphicsContext.hpp"
#include "engine/graphics/MaterialSystem.hpp"
#include "engine/graphics/RenderSystem.hpp"
#include "engine/graphics/Types.hpp"
#include "import/ImportSystem.hpp"

namespace editor {
class Spawner {
public:
    Spawner(engine::graphics::GraphicsContext& graphics_context, engine::core::ResourceManager& resource_manager,
            engine::graphics::RenderSystem& render_system, ImportSystem& import_system)
        : resource_manager_(resource_manager),
          render_system_(render_system),
          graphics_context_(graphics_context),
          import_system_(import_system) {};

    void Spawn(entt::registry& reg, uint64_t asset_id);

private:
    engine::core::ResourceManager& resource_manager_;
    engine::graphics::RenderSystem& render_system_;
    engine::graphics::GraphicsContext& graphics_context_;
    ImportSystem& import_system_;
};

}  // namespace editor