#include "editor/gui/SceneViewportPanel.hpp"

#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

#include "editor/BaseSystem.hpp"
#include "editor/Components.hpp"
#include "editor/gui/EditorGUI.hpp"
#include "engine/core/Components.hpp"

namespace editor {

void SceneViewportPanel::OnUpdate(double dt, const engine::core::InputState& input, entt::registry& reg) {
    if (!is_focused_ && !is_hovered_)
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

    // --- 2. Логика выбора сущности ---
    const auto view = reg.view<SelectedTag, engine::Transform, engine::Relationship>();
    const auto selected_entity = view.front();
    if (selected_entity == entt::null || !reg.valid(selected_entity))
        return;

    const auto cam_view = reg.view<EditorCameraTag, engine::Camera>();
    const auto cam_ent = cam_view.front();
    if (cam_ent == entt::null)
        return;

    const auto& camera = cam_view.get<engine::Camera>(cam_ent);
    auto& transform = reg.get<engine::Transform>(selected_entity);
    const auto& relationship = reg.get<engine::Relationship>(selected_entity);

    // --- 3. Настройка ImGuizmo ---
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist();

    // Важно: Использовать размеры именно контента окна (без заголовков)
    const float window_width = ImGui::GetWindowWidth();
    const float window_height = ImGui::GetWindowHeight();
    ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, window_width, window_height);

    glm::mat4 view_mat = camera.view_matrix;
    const float aspect = static_cast<float>(target_->GetWidth()) / static_cast<float>(target_->GetHeight());
    glm::mat4 proj_mat = glm::perspective(glm::radians(camera.fov), aspect, camera.near_plane, camera.far_plane);

    // Работаем с мировой матрицей
    glm::mat4 modelMatrix = transform.world_matrix;

    // --- 4. Рендер и манипуляция ---
    ImGuizmo::Manipulate(glm::value_ptr(view_mat), glm::value_ptr(proj_mat), current_gizmo_operation_,
                         current_gizmo_mode_, glm::value_ptr(modelMatrix));

    if (ImGuizmo::IsUsing()) {
        glm::mat4 localMatrix = modelMatrix;

        // Если есть родитель, переводим измененную мировую матрицу обратно в локальные координаты
        if (relationship.parent != entt::null && reg.all_of<engine::Transform>(relationship.parent)) {
            const auto& parent_transform = reg.get<engine::Transform>(relationship.parent);
            localMatrix = glm::inverse(parent_transform.world_matrix) * modelMatrix;
        }

        glm::vec3 skew;
        glm::vec4 perspective;
        engine::vec3 translation;
        engine::vec3 scale;
        glm::quat orientation;

        // Разлагаем локальную матрицу на компоненты Transform
        glm::decompose(localMatrix, scale, orientation, translation, skew, perspective);

        transform.position = translation;
        transform.rotation = glm::normalize(orientation);
        transform.scale = scale;
    }
};
}  // namespace editor