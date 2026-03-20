#pragma once

#include <imgui.h>

#include <entt/entt.hpp>
namespace editor {
class InspectorPanel {
   public:
    InspectorPanel() = default;

    void OnImGuiRender(entt::registry& reg);

   private:
    void DrawMetaComponent(entt::registry& reg, entt::entity entity, entt::meta_type type);
    void DrawMetaField(entt::meta_any& instance, entt::meta_data data);
    void DrawUnregisteredComponent(entt::registry& reg, entt::entity entity, entt::id_type id);
    void DrawAddComponentButton(entt::registry& reg, entt::entity entity);
};
}  // namespace editor
