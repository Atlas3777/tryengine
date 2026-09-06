#pragma once

#include <EASTL/span.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <SDL3/SDL_gpu.h>

#include "engine/graphics/RenderCommon.hpp"
#include "engine/graphics/rg/RenderGraph.hpp"

namespace tryengine::graphics {

class DrawPassQueue {
public:
    explicit DrawPassQueue(eastl::string name = "") : name_(std::move(name)) {}

    void Clear() { draw_queue_.clear(); }
    void Submit(const DrawCommand& cmd) { draw_queue_.push_back(cmd); }

    [[nodiscard]] const eastl::string& GetName() const { return name_; }
    [[nodiscard]] eastl::span<const DrawCommand> GetCommands() const { return draw_queue_; }
    [[nodiscard]] bool IsEmpty() const { return draw_queue_.empty(); }

private:
    eastl::string name_;
    eastl::vector<DrawCommand> draw_queue_;
};

using ForwardPass = DrawPassQueue;

void ExecuteDrawCommands(const RGExecuteContext& ctx,
                         CameraData* camera,
                         eastl::span<const DrawCommand> draw_queue);

}  // namespace tryengine::graphics