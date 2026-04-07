#pragma once
#include <imgui.h>

#include <iostream>
#include <ImGuizmo.h>

#include "editor/Components.hpp"
#include "editor/Spawner.hpp"
#include "editor/gui/IPanel.hpp"
#include "engine/core/Components.hpp"

namespace editor {
class SceneViewportPanel : public BaseViewport {
public:
    SceneViewportPanel(SDL_GPUDevice* device, Spawner& spawner) : BaseViewport(device), spawner_(spawner) {}

    const char* GetName() const override { return "Scene"; }

    void OnUpdate(double dt, const engine::core::InputState& input, entt::registry& reg) override;

    void OnImGuiRender(entt::registry& reg) override {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0, 0});
        ImGui::Begin("Scene");

        DrawTexture();

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ID")) {
                const uint64_t asset_id = *static_cast<const uint64_t*>(payload->Data);
                std::cout << "SUCCESS! Dropped file into Viewport: " << asset_id << std::endl;

                spawner_.Spawn(reg, asset_id);
            }
            ImGui::EndDragDropTarget();
        }

        HandleGizmos(reg);

        ImGui::End();
        ImGui::PopStyleVar();
    }

    void OnRender(SDL_GPUCommandBuffer* cmd, engine::graphics::RenderSystem& rs, entt::registry& reg) override {
        const auto mainCamera = reg.view<engine::Camera, EditorCameraTag>().front();
        rs.RenderScene(reg, mainCamera, target_.get(), cmd);
    }

private:
    Spawner& spawner_;
    void HandleGizmos(entt::registry& reg);

    ImGuizmo::OPERATION current_gizmo_operation_ = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE current_gizmo_mode_ = ImGuizmo::LOCAL;
};
}  // namespace editor