#pragma once

#include <EASTL/vector.h>
#include <daScript/misc/vectypes.h>

#include "engine/graphics/rg/RenderGraph.hpp"
#include "engine/resources/ResourceHandle.hpp"

namespace tryengine::graphics {
struct Shader;
}
namespace tryeditor {

struct DebugLine {
    das::float3 start;
    uint8_t r, g, b, a;
    das::float3 end;
    uint32_t _pad;
};

// Линии поверх SceneColor. Шейдер и толщина линий живут в самом пассе.
class DebugPass final : public tryengine::graphics::IRenderPass {
public:
    explicit DebugPass(tryengine::resources::ResourceHandle<tryengine::graphics::Shader> shader) {
        shader_ = shader;
    };
    float thickness = 4.0f;

    const char* Name() const override { return "DebugDraw"; }
    bool IsActive() const override { return static_cast<bool>(shader_); }

    void Collect(tryengine::graphics::CollectContext& ctx) override;
    void Setup(tryengine::graphics::RenderGraphBuilder& builder) override;
    void Execute(tryengine::graphics::RGExecuteContext& ctx) override;

private:
    tryengine::resources::ResourceHandle<tryengine::graphics::Shader> shader_;
    eastl::vector<DebugLine> lines_;

    tryengine::graphics::RGResourceHandle color_;
    tryengine::graphics::RGResourceHandle depth_;
};

}  // namespace tryeditor