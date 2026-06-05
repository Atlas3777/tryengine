#pragma once

#include <imgui.h>

#include <vector>

#include "BaseViewport.hpp"
#include "engine/core/Components.hpp"
#include "engine/graphics/May.hpp"
#include "engine/graphics/RenderSystem.hpp"

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
            ImGui::SetWindowFocus();

            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                SetInputCapture(false);
            }
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }

    void OnRender(SDL_GPUCommandBuffer* cmd, tryengine::graphics::RenderSystem& rs, entt::registry& reg) override {
        const auto mainCamera = reg.view<tryengine::Camera, tryengine::MainCameraTag>().front();
        if (mainCamera == entt::null)
            return;

        // Step 1: Наполняем очередь рендера игровыми объектами
        tryengine::graphics::SubmitSceneFromEnTT(reg, mainCamera, rs);

        // Step 2: Собираем данные игровой камеры с учетом пропорций игрового окна
        auto& cam_transform = reg.get<tryengine::Transform>(mainCamera);
        auto& camera = reg.get<tryengine::Camera>(mainCamera);

        float aspect = static_cast<float>(target_->GetWidth()) / static_cast<float>(target_->GetHeight());

        tryengine::graphics::CameraData camera_data;
        camera_data.view = camera.view_matrix;
        camera_data.proj = glm::perspective(glm::radians(camera.fov), aspect, camera.near_plane, camera.far_plane);
        camera_data.position = cam_transform.position;

        // Step 3: Настройки освещения
        tryengine::graphics::AmbientSettings ambient;
        // Шаг 3: Динамически собираем все источники света из текущей сцены EnTT
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

            scene_lights.push_back(render_light);
        }

        // Шаг 4: Отрисовка
        rs.ExecuteCommands(cmd, target_.get(), camera_data, ambient, scene_lights);
    }
};
}  // namespace tryeditor