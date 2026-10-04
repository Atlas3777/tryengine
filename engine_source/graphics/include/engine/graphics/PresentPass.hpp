#pragma once

#include "engine/graphics/rg/RenderGraph.hpp"

namespace tryengine::graphics {

// Переводит swapchain-image в PRESENT_SRC. Вся работа - в барьере, который строит граф.
class PresentPass final : public IRenderPass {
public:
    const char* Name() const override { return "Present"; }

    void Setup(RenderGraphBuilder& builder) override {
        builder.Read(RGTag_v<"Swapchain">, RGUsageHint::Present);
        builder.MarkSideEffect();
    }

    void Execute(RGExecuteContext&) override {}
};

}  // namespace tryengine::graphics