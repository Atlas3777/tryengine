#include "editor/Components.hpp"
#include "editor/EditorGUI.hpp"
#include "editor/SceneViewportPanel.hpp"
#include "engine/core/Components.hpp"

namespace editor {
void SceneViewportPanel::HandleGizmos(entt::registry& reg) {
    auto view = reg.view<SelectedTag, engine::Transform, engine::Hierarchy>();
    auto selectedE = view.front();
    if (selectedE == entt::null || !reg.valid(selectedE)) return;

    auto camView = reg.view<EditorCameraTag, engine::Camera>();
    auto camEnt = camView.front();
    if (camEnt == entt::null) return;

    auto& camera = camView.get<engine::Camera>(camEnt);
    auto& tc = reg.get<engine::Transform>(selectedE);
    auto& hc = reg.get<engine::Hierarchy>(selectedE);

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowWidth(),
                      ImGui::GetWindowHeight());

    glm::mat4 viewMat = camera.viewMatrix;
    float aspect = (float)target_.GetWidth() / (float)target_.GetHeight();
    glm::mat4 projMat = glm::perspective(glm::radians(camera.fov), aspect, camera.nearPlane, camera.farPlane);

    glm::mat4 modelMatrix = tc.worldMatrix;

    ImGuizmo::Manipulate(glm::value_ptr(viewMat), glm::value_ptr(projMat), m_CurrentGizmoOperation, m_CurrentGizmoMode,
                         glm::value_ptr(modelMatrix));

    if (ImGuizmo::IsUsing()) {
        glm::mat4 localMatrix = modelMatrix;

        if (hc.parent != entt::null && reg.all_of<engine::Transform>(hc.parent)) {
            auto& parentTC = reg.get<engine::Transform>(hc.parent);
            localMatrix = glm::inverse(parentTC.worldMatrix) * modelMatrix;
        }

        glm::vec3 skew;
        glm::vec4 perspective;
        engine::vec3 translation;
        engine::vec3 scale;
        glm::quat orientation;

        glm::decompose(localMatrix, scale, orientation, translation, skew, perspective);

        tc.position = translation;
        tc.rotation = glm::normalize(orientation);
        tc.scale = scale;
    }
};
}  // namespace editor