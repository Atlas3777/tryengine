#pragma once

#include "RuntimeTypes.hpp"
#include "engine/core/Engine.hpp"
#include "engine/core/SlotMap.hpp"
#include "engine/resources/ResourceHandle.hpp"

namespace tryengine::graphics {

class RenderSystem;

class RenderAdapter {
public:
    core::SlotMap<resources::ResourceHandle<Mesh>> slot_map_mesh;
    core::SlotMap<resources::ResourceHandle<Material>> slot_map_material;

    void CollectDrawable(core::Engine& engine, RenderSystem& render_system);

private:
    void CollectDrawCommand(core::Engine& engine, RenderSystem& render_system);
    void CollectLight(core::Engine& engine, RenderSystem& render_system);
};
}  // namespace tryengine::graphics
