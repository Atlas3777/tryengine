#pragma once

#include <SDL3/SDL_gpu.h>
#include <memory>
#include <vector>

#include "engine/graphics/PipelineManager.hpp"
#include "engine/graphics/RenderTarget.hpp"
#include "engine/graphics/RenderCommon.hpp" // Тут лежат наши новые структуры

namespace tryengine::graphics {

class RenderSystem {
public:
    RenderSystem(SDL_GPUDevice* device);
    ~RenderSystem() = default;

    // 1. Очистка очереди перед кадром
    void ClearQueue();

    // 2. Интерфейс для внешних систем (C++, daslang через C-binding и т.д.)
    void Submit(const DrawCommand& cmd);

    // 3. Выполнение рендеринга накопленной очереди
    void ExecuteCommands(SDL_GPUCommandBuffer* cmd_buffer,
                         RenderTarget* target,
                         const CameraData& camera,
                         const AmbientSettings& ambient,
                         const std::vector<Light>& lights);

    PipelineManager* GetPipelineManager() { return pipeline_manager_.get(); }

private:
    SDL_GPUDevice* device_ = nullptr;
    std::unique_ptr<PipelineManager> pipeline_manager_;

    // Внутренний буфер команд на кадр
    std::vector<DrawCommand> draw_queue_;
};

}  // namespace tryengine::graphics