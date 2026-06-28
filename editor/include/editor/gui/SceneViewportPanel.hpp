#pragma once

#include <imgui.h>

#include <ImGuizmo.h>

#include <entt/entity/registry.hpp>
#include <iostream>
#include <vector>

#include "BaseViewport.hpp"
#include "editor/Components.hpp"
#include "editor/Spawner.hpp"
#include "editor/gui/IPanel.hpp"
#include "editor/utils/EditorGUIUtils.hpp"
#include "engine/core/Components.hpp"
#include "engine/graphics/May.hpp"
#include "engine/graphics/RenderSystem.hpp"

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
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_BROWSER_ITEM")) {
                if (const auto* data = static_cast<const AssetPayload*>(payload->Data);
                    data->expected_asset_type == tryengine::AssetType::Gltf)
                {
                    std::cout << "Spawning asset ID: " << data->asset_id << std::endl;
                    spawner_.Spawn(reg, data->asset_id);
                } else {
                    std::cout << "Wrong asset type for spawning!" << std::endl;
                }
            }
            ImGui::EndDragDropTarget();
        }

        HandleGizmos(reg);

        ImGui::End();
        ImGui::PopStyleVar();
    }

    void OnRender(SDL_GPUCommandBuffer* cmd, tryengine::graphics::RenderSystem& rs, entt::registry& reg) override {
        const auto editor_camera = reg.view<tryengine::Camera, EditorCameraTag>().front();
        if (editor_camera == entt::null) return;

        // Step 1: Наполняем независимую от ECS очередь команд рендера через EnTT-заглушку
        tryengine::graphics::SubmitSceneFromEnTT(reg, editor_camera, rs);

        // Step 2: Вычисляем параметры камеры на основе текущего размера текстуры вьюпорта
        auto& cam_transform = reg.get<tryengine::Transform>(editor_camera);
        auto& camera = reg.get<tryengine::Camera>(editor_camera);

        float aspect = static_cast<float>(target_->GetWidth()) / static_cast<float>(target_->GetHeight());

        tryengine::graphics::CameraData camera_data;
        camera_data.view = camera.view_matrix;
        camera_data.proj = glm::perspective(glm::radians(camera.fov), aspect, camera.near_plane, camera.far_plane);
        camera_data.position = cam_transform.position;


        std::vector<tryengine::graphics::Light> scene_lights;

        auto light_view = reg.view<tryengine::Transform, tryengine::LightComponent>();
        for (auto entity : light_view) {
            const auto& t = light_view.get<tryengine::Transform>(entity);
            const auto& l = light_view.get<tryengine::LightComponent>(entity);

            tryengine::graphics::Light render_light;
            render_light.position = t.position;
            render_light.intensity = l.intensity;
            render_light.color = l.color;
            render_light.radius = l.radius;
            render_light.type = tryengine::graphics::LightType::Point;

            scene_lights.push_back(render_light);
        }

        // Step 4: Выполняем отрисовку отсортированной очереди команд с оптимизацией стейтов GPU
        rs.ExecuteCommands(cmd, *target_, camera_data, scene_lights);
    }

private:
    void HandleGizmos(entt::registry& reg);
    bool is_camera_controlled_ = false;
    ImGuizmo::OPERATION current_gizmo_operation_ = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE current_gizmo_mode_ = ImGuizmo::LOCAL;

    Spawner& spawner_;
};
}  // namespace tryeditor