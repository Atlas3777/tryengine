#pragma once

#include <entt/entt.hpp>

#include "editor/SelectionManager.hpp"
#include "editor/asset_inspector/AssetInspectorManager.hpp"
#include "editor/gui/IPanel.hpp"
#include "editor/import/ImportSystem.hpp"
namespace tryeditor {
class AddressablesProvider;
class InspectorPanel : public IPanel {
public:
    InspectorPanel(SelectionManager& editor_context, ImportSystem& import_system_,
                   AssetInspectorManager& asset_inspector_manager_, AddressablesProvider& addressables_provider)
        : import_system_(import_system_),
          asset_inspector_manager_(asset_inspector_manager_),
          selection_manager_(editor_context),
    addressables_provider_(addressables_provider){};
    const char* GetName() const override { return "Game"; }

    void OnImGuiRender(entt::registry& reg);

private:
    void DrawEntityInspector(entt::registry& reg);
    void DrawAssetInspector(const std::filesystem::path& path) const;
    void DrawAssetFooter();


    void DrawMetaComponent(entt::registry& reg, entt::entity entity, entt::meta_type type);
    void DrawMetaField(entt::meta_any& instance, entt::meta_data data);
    void DrawUnregisteredComponent(entt::registry& reg, entt::entity entity, entt::id_type id);
    void DrawAddComponentButton(entt::registry& reg, entt::entity entity);

    ImportSystem& import_system_;
    AssetInspectorManager& asset_inspector_manager_;
    SelectionManager& selection_manager_;
    AddressablesProvider& addressables_provider_;
    bool show_settings_window_ = false;
};
}  // namespace tryeditor
