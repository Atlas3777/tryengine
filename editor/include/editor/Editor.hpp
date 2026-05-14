#pragma once
#include <memory>
#include <string>

#include "engine/core/GameAPI.hpp"
#include "engine/graphics/GraphicsContext.hpp"

namespace tryeditor {
class ReflectionSystem;
class AddressablesProvider;
class SelectionManager;
class AssetInspectorManager;
class AssetsFactoryManager;
class ImportSystem;
class EditorGUI;
class ControllerManager;
class SceneManagerController;
class Spawner;
class Editor {
public:
    Editor(tryengine::core::Engine& engine, tryengine::graphics::GraphicsContext& graphics_context);
    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;
    Editor(Editor&&) noexcept = delete;
    Editor& operator=(Editor&&) noexcept = delete;

    ~Editor();

    void Init();

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
    std::unique_ptr<SelectionManager> selection_manager_;
    std::unique_ptr<EditorGUI> editor_gui_;
    std::unique_ptr<Spawner> spawner_;
    std::unique_ptr<AddressablesProvider> addressables_provider_;
    std::unique_ptr<ReflectionSystem> reflection_system_;
    std::unique_ptr<ControllerManager> gui_controller_manager_;

    tryengine::graphics::GraphicsContext& graphics_context_;
    tryengine::core::Engine& engine_;
};

}  // namespace tryeditor
