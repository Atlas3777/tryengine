#pragma once
#include <imgui.h>

#include <iostream>
#include <ImGuizmo.h>

#include "editor/Components.hpp"
#include "editor/Spawner.hpp"
#include "editor/gui/IPanel.hpp"
#include "engine/core/Components.hpp"

namespace tryeditor {
class SceneViewportPanel : public BaseViewport {
public:
    SceneViewportPanel(tryengine::graphics::GraphicsContext& context, Spawner& spawner) : BaseViewport(context), spawner_(spawner) {}

    const char* GetName() const override { return "Scene"; }

    void OnUpdate(double dt, const tryengine::core::InputState& input, entt::registry& reg) override;

    void OnImGuiRender(entt::registry& reg) override {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0, 0});
        ImGui::Begin("Scene");

        if (is_input_captured_) {
            ImGuiIO& io = ImGui::GetIO();
            io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
        }

        DrawTexture();

        // 1. Начинаем захват при зажатии ПКМ
        if (is_hovered_ && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            SetInputCapture(true);
        }

        // 2. Если захвачено - держим фокус и ждем отпускания
        if (is_input_captured_) {
            ImGui::SetWindowFocus();

            if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                SetInputCapture(false);
            }
        }

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

    void OnRender(SDL_GPUCommandBuffer* cmd, tryengine::graphics::RenderSystem& rs, entt::registry& reg) override {
        const auto mainCamera = reg.view<tryengine::Camera, EditorCameraTag>().front();
        rs.RenderScene(reg, mainCamera, target_.get(), cmd);
    }

private:
    void HandleGizmos(entt::registry& reg);
    bool is_camera_controlled_ = false;
    ImGuizmo::OPERATION current_gizmo_operation_ = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE current_gizmo_mode_ = ImGuizmo::LOCAL;

    Spawner& spawner_;
};
}  // namespace tryeditor