#pragma once
#include <imgui.h>

#include "IPanel.hpp"
#include "engine/core/Components.hpp"

namespace tryeditor {
class GameViewportPanel : public BaseViewport {
public:
    GameViewportPanel(tryengine::graphics::GraphicsContext& context) : BaseViewport(context) {}
    const char* GetName() const override { return "Game"; }

    void OnImGuiRender(entt::registry& reg) override {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0, 0});
        ImGui::Begin("Game");

        DrawTexture();

        // 1. Захватываем ввод по обычному клику (ЛКМ) на окно игры
        if (is_hovered_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            SetInputCapture(true);
        }

        // 2. Если мы в игре
        if (is_input_captured_) {
            // Удерживаем фокус на окне игры, чтобы ImGui отправлял клавиатуру сюда
            ImGui::SetWindowFocus();

            // Освобождаем мышь по нажатию Escape
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                SetInputCapture(false);
            }
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }
    void OnRender(SDL_GPUCommandBuffer* cmd, tryengine::graphics::RenderSystem& rs, entt::registry& reg) override {
        const auto mainCamera = reg.view<tryengine::Camera, tryengine::MainCameraTag>().front();
        rs.RenderScene(reg, mainCamera, target_.get(), cmd);
    }
};
}  // namespace tryeditor