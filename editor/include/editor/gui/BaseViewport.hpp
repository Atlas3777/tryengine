#pragma once

#include <SDL3/SDL_mouse.h>

#include "IPanel.hpp"
#include "engine/graphics/GraphicsContext.hpp"
#include "engine/graphics/RenderTarget.hpp"

namespace tryengine::graphics {
class GraphicsContext;
}
namespace tryeditor {
class BaseViewport : public IPanel {
protected:
    tryengine::graphics::GraphicsContext& graphics_context_;
    std::unique_ptr<tryengine::graphics::RenderTarget> target_ = nullptr;

    bool is_hovered_ = false;
    bool is_focused_ = false;
    bool is_input_captured_ = false;

    BaseViewport(tryengine::graphics::GraphicsContext& context) : graphics_context_(context) {
        target_ = std::make_unique<tryengine::graphics::RenderTarget>(graphics_context_.GetDevice(), 600, 800,
                                                                      SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
    }

    void DrawTexture() {
        ImVec2 size = ImGui::GetContentRegionAvail();
        if (size.x > 0 && size.y > 0) {
            if (size.x != target_->GetWidth() || size.y != target_->GetHeight()) {
                target_->Resize((uint32_t) size.x, (uint32_t) size.y);
            }
            ImGui::Image((ImTextureID) target_->GetColor(), size);
        }

        is_hovered_ = ImGui::IsWindowHovered();
        is_focused_ = ImGui::IsWindowFocused();
    }

    // Хелпер для захвата/освобождения курсора
    void SetInputCapture(bool capture) {
        is_input_captured_ = capture;
        SDL_SetWindowRelativeMouseMode(graphics_context_.GetWindow(), capture);
    }
};
}  // namespace tryeditor