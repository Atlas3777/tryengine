#pragma once
#include <imgui.h>

#include <entt/entity/fwd.hpp>

#include "engine/core/InputState.hpp"
#include "engine/graphics/RenderSystem.hpp"
#include "engine/graphics/RenderTarget.hpp"

namespace editor {
class IPanel {
   public:
    bool is_visible_ = true;
    virtual ~IPanel() = default;
    virtual const char* GetName() const = 0;
    virtual void OnUpdate(double dt, const engine::core::InputState& input, entt::registry& reg) {}
    virtual void OnRender(SDL_GPUCommandBuffer* cmd, engine::graphics::RenderSystem& rs, entt::registry& reg) {}
    virtual void OnImGuiRender(entt::registry& reg) = 0;
};

class BaseViewport : public IPanel {
   protected:

    std::unique_ptr<engine::graphics::RenderTarget> target_ = nullptr;
    bool is_hovered_ = false;
    bool is_focused_ = false;

    BaseViewport(SDL_GPUDevice* device) {
        target_ = std::make_unique<engine::graphics::RenderTarget>(device, 600,800,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
    }

    void DrawTexture() {
        ImVec2 size = ImGui::GetContentRegionAvail();
        if (size.x > 0 && size.y > 0) {
            // Проверка ресайза здесь
            if (size.x != target_->GetWidth() || size.y != target_->GetHeight()) {
                target_->Resize((uint32_t)size.x, (uint32_t)size.y);
            }
            ImGui::Image((ImTextureID)target_->GetColor(), size);
        }
        is_hovered_ = ImGui::IsWindowHovered();
        is_focused_ = ImGui::IsWindowFocused();
    }
};

}  // namespace editor
