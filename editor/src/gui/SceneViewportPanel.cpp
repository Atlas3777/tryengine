#include "editor/gui/SceneViewportPanel.hpp"

#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

#include "editor/BaseSystem.hpp"
#include "editor/Components.hpp"
#include "editor/gui/EditorGUI.hpp"
#include "engine/core/Components.hpp"

namespace tryeditor {

void SceneViewportPanel::OnUpdate(double dt, const tryengine::core::InputState& input, entt::registry& reg) {
    if (!is_focused_ && !is_hovered_)
        return;
    if (!is_input_captured_) return;
    UpdateEditorCameraSystem(reg, dt, input);
}

void SceneViewportPanel::HandleGizmos(entt::registry& reg) {
    if (ImGui::IsWindowFocused()) {
        if (ImGui::IsKeyPressed(ImGuiKey_W))
            current_gizmo_operation_ = ImGuizmo::TRANSLATE;
        if (ImGui::IsKeyPressed(ImGuiKey_E))
            current_gizmo_operation_ = ImGuizmo::ROTATE;
        if (ImGui::IsKeyPressed(ImGuiKey_R))
            current_gizmo_operation_ = ImGuizmo::SCALE;

        if (ImGui::IsKeyPressed(ImGuiKey_G)) {
            current_gizmo_mode_ = (current_gizmo_mode_ == ImGuizmo::LOCAL) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
        }
    }

    const auto view = reg.view<SelectedTag, tryengine::Transform, tryengine::Relationship>();
    const auto selected_entity = view.front();
    if (selected_entity == entt::null || !reg.valid(selected_entity))
        return;

    const auto cam_view = reg.view<EditorCameraTag, tryengine::Camera>();
    const auto cam_ent = cam_view.front();
    if (cam_ent == entt::null)
        return;

    const auto& camera = cam_view.get<tryengine::Camera>(cam_ent);
    auto& transform = reg.get<tryengine::Transform>(selected_entity);
    const auto& relationship = reg.get<tryengine::Relationship>(selected_entity);

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist();

    // --- ПРОКАЧКА 1: Правильный расчет области отрисовки ---
    // Это предотвратит смещение гизмо, если у окна есть заголовок (title bar)
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 minRegion = ImGui::GetWindowContentRegionMin();
    ImVec2 maxRegion = ImGui::GetWindowContentRegionMax();
    float viewportX = windowPos.x + minRegion.x;
    float viewportY = windowPos.y + minRegion.y;
    float viewportWidth = maxRegion.x - minRegion.x;
    float viewportHeight = maxRegion.y - minRegion.y;
    ImGuizmo::SetRect(viewportX, viewportY, viewportWidth, viewportHeight);


    glm::mat4 view_mat = camera.view_matrix;
    const float aspect = static_cast<float>(target_->GetWidth()) / static_cast<float>(target_->GetHeight());
    glm::mat4 proj_mat = glm::perspective(glm::radians(camera.fov), aspect, camera.near_plane, camera.far_plane);
    glm::mat4 modelMatrix = transform.world_matrix;

    // --- ПРОКАЧКА 3: Привязка к сетке (Snapping) по зажатому Ctrl ---
    bool snap = ImGui::IsKeyDown(ImGuiKey_LeftCtrl);
    float snapValue = 0.5f; // Шаг для перемещения и скейла
    if (current_gizmo_operation_ == ImGuizmo::ROTATE) {
        snapValue = 45.0f; // Шаг для вращения (45 градусов)
    }
    float snapValues[3] = { snapValue, snapValue, snapValue };

    // --- 4. Рендер и манипуляция ---
    ImGuizmo::Manipulate(
        glm::value_ptr(view_mat),
        glm::value_ptr(proj_mat),
        current_gizmo_operation_,
        current_gizmo_mode_,
        glm::value_ptr(modelMatrix),
        nullptr, // deltaMatrix
        snap ? snapValues : nullptr // Передаем массив привязки, если зажат Ctrl
    );

    if (ImGuizmo::IsUsing()) {
        glm::mat4 localMatrix = modelMatrix;

        if (relationship.parent != entt::null && reg.all_of<tryengine::Transform>(relationship.parent)) {
            const auto& parent_transform = reg.get<tryengine::Transform>(relationship.parent);
            localMatrix = glm::inverse(parent_transform.world_matrix) * modelMatrix;
        }

        glm::vec3 skew;
        glm::vec4 perspective;
        tryengine::vec3 translation;
        tryengine::vec3 scale;
        glm::quat orientation;

        glm::decompose(localMatrix, scale, orientation, translation, skew, perspective);

        transform.position = translation;
        transform.rotation = glm::normalize(orientation);
        transform.scale = scale;
    }
};
}  // namespace tryeditor