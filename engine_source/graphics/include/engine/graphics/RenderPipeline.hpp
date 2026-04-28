#pragma once
#include "IRenderPass.hpp"

namespace tryengine::graphics {
class RenderPipeline {
   public:
    explicit RenderPipeline();
    void Execute();

   private:
    std::vector<IRenderPass*> RenderPasses;
};
}  // namespace tryengine::graphics