#pragma once

#include <SDL3/SDL_gpu.h>
#include <memory>
#include <EASTL/span.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>

#include "engine/graphics/PipelineManager.hpp"
#include "engine/graphics/RenderCommon.hpp"
#include "engine/graphics/RenderPass.hpp"
#include "engine/graphics/RenderTarget.hpp"
#include "engine/graphics/rg/RenderGraph.hpp"

namespace tryengine::graphics {

class RenderSystem {
public:
    explicit RenderSystem(SDL_GPUDevice* device);
    ~RenderSystem();

    template <typename PassT = DrawPassQueue>
    PassT& GetOrCreatePass(eastl::string_view name) {
        for (auto& pass : pass_queues_) {
            if (pass->GetName() == name) {
                return *pass;
            }
        }
        auto pass = std::make_unique<DrawPassQueue>(eastl::string(name));
        DrawPassQueue& ref = *pass;
        pass_queues_.push_back(std::move(pass));
        return ref;
    }

    void ClearAllPasses();

    void RenderToTarget(SDL_GPUCommandBuffer* cmd_buffer,
                        RenderTarget& target,
                        CameraData& camera,
                        const AmbientSettings& ambient,
                        eastl::span<const PointLightGPU> point_lights);

    PipelineManager* GetPipelineManager() { return pipeline_manager_.get(); }
    RenderGraph& GetRenderGraph() { return render_graph_; }

private:
    SDL_GPUDevice* device_ = nullptr;
    std::unique_ptr<PipelineManager> pipeline_manager_;
    RenderGraph render_graph_;

    eastl::vector<std::unique_ptr<DrawPassQueue>> pass_queues_;
};

}  // namespace tryengine::graphics