#pragma once
#include <imgui.h>

#include "Components.hpp"
#include "IPanel.hpp"
#include "engine/core/Components.hpp"
namespace editor {
class SceneViewportPanel : public BaseViewport {
   public:
    SceneViewportPanel(SDL_GPUDevice* device):BaseViewport(device){}

    const char* GetName() const override { return "Scene"; }

    void OnImGuiRender(entt::registry& reg) override {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0, 0});
        ImGui::Begin("Scene");

        DrawTexture();
        HandleGizmos(reg);  // Твой текущий код из EditorGUI

        ImGui::End();
        ImGui::PopStyleVar();
    }

    void OnRender(SDL_GPUCommandBuffer* cmd, engine::graphics::RenderSystem& rs, entt::registry& reg) override {
        const auto mainCamera = reg.view<engine::Camera, EditorCameraTag>().front();
        rs.RenderScene(reg, mainCamera, target_.get(), cmd);
    }

   private:
    void HandleGizmos(entt::registry& reg);
};
}  // namespace editor