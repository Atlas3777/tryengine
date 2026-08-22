#pragma once

#include <SDL3/SDL_gpu.h>
#include <memory>
#include <EASTL/span.h>
#include <EASTL/vector.h>

#include "engine/graphics/PipelineManager.hpp"
#include "engine/graphics/RenderTarget.hpp"
#include "engine/graphics/RenderCommon.hpp"

namespace tryengine::graphics {

class RenderSystem {
public:
    RenderSystem(SDL_GPUDevice* device);
    ~RenderSystem();

    AmbientSettings ambient;

    __forceinline void ClearQueue() { draw_queue_.clear(); }
    __forceinline void Submit(const DrawCommand& cmd) { draw_queue_.push_back(cmd); }

    void RenderToTarget(SDL_GPUCommandBuffer* cmd_buffer,
                         RenderTarget& target,
                         CameraData& camera);

    PipelineManager* GetPipelineManager() { return pipeline_manager_.get(); }

    eastl::span<PointLightGPU> lights_queue_;


private:
    SDL_GPUDevice* device_ = nullptr;
    std::unique_ptr<PipelineManager> pipeline_manager_;

    eastl::vector<DrawCommand> draw_queue_;

    SDL_GPUBuffer* light_storage_buffer_ = nullptr;
    size_t current_buffer_capacity_ = 0;
};

}  // namespace tryengine::graphics