#pragma once

#include <EASTL/span.h>
#include <EASTL/vector.h>

#include "LightPass.hpp"
#include "engine/graphics/MeshDrawCall.hpp"
#include "engine/graphics/rg/RenderGraph.hpp"

namespace tryengine::graphics {

class OpaquePass final : public IRenderPass {
public:
    const char* Name() const override { return "Opaque"; }

    void Collect(CollectContext& ctx) override;
    void Setup(RenderGraphBuilder& builder) override;
    void Execute(RGExecuteContext& ctx) override;

    // Очередь отсортирована в Collect; Picking переиспользует её без повторного сбора.
    [[nodiscard]] eastl::span<const MeshDrawCall> DrawQueue() const { return draw_queue_; }

private:
    void RecordDraws(RGExecuteContext& ctx);

    VkExtent2D viewport_{};
    eastl::vector<MeshDrawCall> draw_queue_;
    eastl::vector<PointLightGPU> point_lights_;

    RGResourceHandle color_;
    RGResourceHandle depth_;
};

}  // namespace tryengine::graphics