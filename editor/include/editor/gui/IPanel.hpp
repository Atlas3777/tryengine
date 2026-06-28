#pragma once

#include <SDL3/SDL_gpu.h>
#include <entt/entity/fwd.hpp>

namespace tryengine::graphics {
class RenderSystem;
}
namespace tryengine::core {
struct InputState;
}
namespace tryeditor {

class IPanel {
public:
    bool is_visible_ = true;
    virtual ~IPanel() = default;
    virtual const char* GetName() const = 0;
    virtual void OnUpdate(double dt, const tryengine::core::InputState& input, entt::registry& reg) {}
    virtual void OnRender(SDL_GPUCommandBuffer* cmd, tryengine::graphics::RenderSystem& rs, entt::registry& reg) {}
    virtual void OnImGuiRender(entt::registry& reg) = 0;
};

}  // namespace tryeditor