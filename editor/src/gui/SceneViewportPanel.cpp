#include "editor/gui/SceneViewportPanel.hpp"

#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

#include "editor/BaseSystem.hpp"
#include "editor/Components.hpp"
#include "engine/core/Components.hpp"

namespace tryeditor {

void SceneViewportPanel::OnUpdate(double dt, const tryengine::core::InputState& input, entt::registry& reg) {
    if (!is_focused_ && !is_hovered_)
        return;
    if (!is_input_captured_)
        return;
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

    ImVec2 viewport_min = ImGui::GetItemRectMin();
    ImVec2 viewport_size = ImGui::GetItemRectSize();

    float viewport_x = viewport_min.x;
    float viewport_y = viewport_min.y;
    float viewport_width = viewport_size.x;
    float viewport_height = viewport_size.y;

    ImGuizmo::SetRect(viewport_x, viewport_y, viewport_width, viewport_height);

    glm::mat4 view_mat = camera.view_matrix;
    const float aspect = static_cast<float>(target_->GetWidth()) / static_cast<float>(target_->GetHeight());
    glm::mat4 proj_mat = glm::perspective(glm::radians(camera.fov), aspect, camera.near_plane, camera.far_plane);
    glm::mat4 model_matrix = transform.world_matrix;

    // Привязка к сетке (Snapping) по зажатому Ctrl
    bool snap = ImGui::IsKeyDown(ImGuiKey_LeftCtrl);
    float snap_value = 0.5f;  // Шаг для перемещения и скейла
    if (current_gizmo_operation_ == ImGuizmo::ROTATE) {
        snap_value = 45.0f;  // Шаг для вращения (45 градусов)
    }
    float snapValues[3] = {snap_value, snap_value, snap_value};

    ImGuizmo::Manipulate(glm::value_ptr(view_mat), glm::value_ptr(proj_mat), current_gizmo_operation_,
                         current_gizmo_mode_, glm::value_ptr(model_matrix),
                         nullptr,                     // deltaMatrix
                         snap ? snapValues : nullptr  // Передаем массив привязки, если зажат Ctrl
    );

    if (ImGuizmo::IsUsing()) {
        glm::mat4 localMatrix = model_matrix;

        if (relationship.parent != entt::null && reg.all_of<tryengine::Transform>(relationship.parent)) {
            const auto& parent_transform = reg.get<tryengine::Transform>(relationship.parent);
            localMatrix = glm::inverse(parent_transform.world_matrix) * model_matrix;
        }

        glm::vec3 skew;
        glm::vec4 perspective;
        glm::vec3 translation;
        glm::vec3 scale;
        glm::quat orientation;

        glm::decompose(localMatrix, scale, orientation, translation, skew, perspective);

        transform.position = translation;
        transform.rotation = glm::normalize(orientation);
        transform.scale = scale;
    }
};
}  // namespace tryeditor