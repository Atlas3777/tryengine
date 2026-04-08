#pragma once
#include <imgui.h>

#include "IPanel.hpp"
#include "engine/core/Components.hpp"

namespace tryeditor {
class GameViewportPanel : public BaseViewport {
public:
    GameViewportPanel(SDL_GPUDevice* device) : BaseViewport(device) {}
    const char* GetName() const override { return "Game"; }

    void OnImGuiRender(entt::registry& reg) override {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0, 0});
        ImGui::Begin("Game");

        // Тут можно добавить логику поиска "активной" камеры в ECS
        // Если камеры нет — рисовать заглушку "No Camera Found"
        DrawTexture();

        ImGui::End();
        ImGui::PopStyleVar();
    }
    void OnRender(SDL_GPUCommandBuffer* cmd, tryengine::graphics::RenderSystem& rs, entt::registry& reg) override {
        const auto mainCamera = reg.view<tryengine::Camera, tryengine::MainCameraTag>().front();
        rs.RenderScene(reg, mainCamera, target_.get(), cmd);
    }
};
}  // namespace tryeditor