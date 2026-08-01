#pragma once

#include <memory>

#include "engine/core/Engine.hpp"
#include "engine/graphics/GraphicsContext.hpp"

namespace tryeditor {
class AssetSourceDatabase;
class ControllerManager;
class EditorGUI;
class Spawner;
class SelectionManager;
class AssetInspectorManager;
class AssetsFactoryManager;
class ImportSystem;
class Editor {
public:
    Editor(tryengine::core::Engine& engine, tryengine::graphics::GraphicsContext& graphics_context);
    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;
    Editor(Editor&&) noexcept = delete;
    Editor& operator=(Editor&&) noexcept = delete;

    ~Editor();

    void SaveScene();
    void SaveSceneForPlayMode();
    void LoadDefaultScene() const;

    void RegisterResourceLoaders() const;
    void RegisterAssetsImporters() const;
    void RegisterAssetsFactories() const;
    void RegisterAssetsInspector() const;

    bool running = false;
    bool play_mode = false;

    tryengine::core::Engine& GetEngine() { return engine_; }
    tryengine::graphics::GraphicsContext& GetGraphicsContext() { return graphics_context_; }

    // Геттеры для редакторских подсистем
    ImportSystem& GetImportSystem() { return *import_system_; }
    AssetsFactoryManager& GetAssetsFactory() { return *assets_factory_; }
    AssetInspectorManager& GetAssetInspector() { return *asset_inspector_manager_; }
    SelectionManager& GetSelectionManager() { return *selection_manager_; }
    Spawner& GetSpawner() { return *spawner_; }
    EditorGUI& GetGUI() { return *gui_; }
    AssetSourceDatabase& GetAssetSourceDatabase() { return *asset_source_database_; }

private:
    tryengine::core::Engine& engine_;
    tryengine::graphics::GraphicsContext& graphics_context_;

    std::unique_ptr<AssetSourceDatabase> asset_source_database_;

    std::unique_ptr<ImportSystem> import_system_;
    std::unique_ptr<AssetsFactoryManager> assets_factory_;
    std::unique_ptr<AssetInspectorManager> asset_inspector_manager_;
    std::unique_ptr<SelectionManager> selection_manager_;
    std::unique_ptr<Spawner> spawner_;
    std::unique_ptr<ControllerManager> gui_controller_manager_;
    std::unique_ptr<EditorGUI> gui_;
};
}
