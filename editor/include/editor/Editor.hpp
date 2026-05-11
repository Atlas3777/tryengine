#pragma once
#include <memory>
#include <string>

#include "editor/asset_factories/AssetsFactoryManager.hpp"
#include "editor/asset_inspector/AssetInspectorManager.hpp"
#include "editor/gui/EditorGUI.hpp"
#include "editor/import/ImportSystem.hpp"
#include "engine/core/GameAPI.hpp"

namespace tryeditor {
class Spawner;
class Editor {
public:
    Editor(tryengine::core::Engine& engine, tryengine::graphics::GraphicsContext& graphics_context);
    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;
    Editor(Editor&&) noexcept = delete;
    Editor& operator=(Editor&&) noexcept = delete;

    ~Editor();

    bool LoadGameLibrary(const std::string& original_path);
    void UnloadGameLibrary();

    void SaveScene();
    void SaveSceneForPlayMode();
    void LoadDefaultScene() const;

    void RegisterComponents() const;
    void RegisterResourceLoaders() const;
    void RegisterAssetsImporters() const;
    void RegisterAssetsFactories() const;
    void RegisterAssetsInspector() const;

    EditorGUI& GetEditorGUI() { return *editor_gui_; }
    ImportSystem& GetImportSystem() { return *import_system_; }

    bool running = false;
    bool play_mode = false;
    tryengine::core::GameLibrary game_lib;

private:
    std::unique_ptr<ImportSystem> import_system_;
    std::unique_ptr<AssetsFactoryManager> assets_factory_;
    std::unique_ptr<AssetInspectorManager> asset_inspector_manager_;
    std::unique_ptr<EditorContext> editor_context_;
    std::unique_ptr<EditorGUI> editor_gui_;
    std::unique_ptr<Spawner> spawner_;
    std::unique_ptr<AddressablesProvider> addressables_provider_;

    tryengine::graphics::GraphicsContext& graphics_context_;
    tryengine::core::Engine& engine_;
};

}  // namespace tryeditor
