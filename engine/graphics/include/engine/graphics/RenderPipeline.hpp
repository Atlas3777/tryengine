#pragma once
#include "IRenderPass.hpp"

namespace engine::graphics {
class RenderPipeline {
   public:
    explicit RenderPipeline();
    void Execute();

   private:
    std::vector<IRenderPass*> RenderPasses;
};
}  // namespace engine::graphics