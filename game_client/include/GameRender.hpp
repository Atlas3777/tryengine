#pragma once

#include "engine/core/Engine.hpp"
#include "engine/graphics/GraphicsContext.hpp"
#include "engine/graphics/PipelineManager.hpp"
#include "engine/graphics/rg/RenderGraph.hpp"

namespace trygame {

class GameRender {
public:
    explicit GameRender(tryengine::graphics::GraphicsContext& context,
                        tryengine::graphics::RenderGraph& render_graph);
    ~GameRender() = default;

    void Render(tryengine::core::Engine& engine,
                tryengine::graphics::GraphicsContext& context);

private:
    tryengine::graphics::RenderGraph& rg_;
    tryengine::graphics::PipelineManager pm_;
};

}  // namespace trygame