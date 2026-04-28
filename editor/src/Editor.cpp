#include "editor/Editor.hpp"

#include <dlfcn.h>
#include <filesystem>
#include <iostream>

#include "editor/Components.hpp"
#include "editor/Spawner.hpp"
#include "editor/asset_factories/MaterialAssetFactory.hpp"
#include "editor/asset_factories/ShaderAssetFactory.hpp"
#include "editor/asset_inspector/MaterialAssetInspector.hpp"
#include "editor/asset_inspector/ShaderAssetInspector.hpp"
#include "editor/asset_inspector/TextureAssetInspector.hpp"
#include "editor/import/GlslShaderImporter.hpp"
#include "editor/import/GltfImporter.hpp"
#include "editor/import/TextureImporter.hpp"
#include "engine/core/Components.hpp"
#include "engine/core/ResourceManager.hpp"
#include "engine/graphics/MaterialLoader.hpp"
#include "engine/graphics/MeshLoader.hpp"
#include "engine/graphics/ShaderAssetLoader.hpp"
#include "engine/graphics/Types.hpp"
#include "engine/resources/MeshDataLoader.hpp"
#include "engine/resources/TextureLoader.hpp"
#include "engine/resources/Types.hpp"
#include "game/GameAPI.hpp"

namespace tryeditor {

Editor::Editor(tryengine::core::Engine& eng, tryengine::graphics::GraphicsContext& graphics_context,
               tryengine::graphics::RenderSystem& render_system)
    : graphics_context_(graphics_context), engine_(eng) {
    editor_context_ = std::make_unique<EditorContext>();
    asset_inspector_manager_ = std::make_unique<AssetInspectorManager>();

    assets_factory_ = std::make_unique<AssetsFactoryManager>();
    import_system_ = std::make_unique<ImportSystem>();
    spawner_ = std::make_unique<Spawner>(graphics_context, eng.GetResourceManager(), render_system, *import_system_);
    editor_gui_ = std::make_unique<EditorGUI>(graphics_context, *import_system_, *spawner_, *editor_context_,
                                              *assets_factory_, *asset_inspector_manager_);
}

Editor::~Editor() {
    UnloadGameLibrary();
}

void Editor::RegisterAssetsFactories() const {
    // Передаем ImportSystem в фабрики
    assets_factory_->RegisterFactory<ShaderAssetFactory>(*import_system_);
    assets_factory_->RegisterFactory<MaterialAssetFactory>(*import_system_);
}

void Editor::RegisterAssetsInspector() const {
    // Передаем ImportSystem в инспекторы
    asset_inspector_manager_->RegisterInspector<ShaderAssetInspector>("shader", *import_system_);
    asset_inspector_manager_->RegisterInspector<TextureAssetInspector>("texture", *import_system_);
    asset_inspector_manager_->RegisterInspector<MaterialAssetInspector>("material", *import_system_);
}

void Editor::RegisterAssetsImporters() const {
    import_system_->RegisterImporter<GlslShaderImporter, GlslShaderImportSettings>({".vert", ".frag"});
    import_system_->RegisterImporter<GltfImporter, GltfImportSettings>({".glb", ".gltf"}, *assets_factory_, *import_system_);
    // import_system_->RegisterImporter<TextureImporter, TextureImportSettings>({".png", ".jpg"});
}

void Editor::RegisterResourceLoaders() const {
    auto& res_manager_ = engine_.GetResourceManager();
    res_manager_.RegisterLoader<tryengine::resources::MeshData>(tryengine::resources::MeshDataLoader(res_manager_));
    res_manager_.RegisterLoader<tryengine::graphics::Mesh>(
        tryengine::graphics::MeshLoader(res_manager_, graphics_context_.GetDevice()));

    res_manager_.RegisterLoader<tryengine::graphics::Texture>(
        tryengine::graphics::TextureLoader(res_manager_, graphics_context_.GetDevice()));
    res_manager_.RegisterLoader<tryengine::graphics::Shader>(
        tryengine::graphics::ShaderAssetLoader(res_manager_, graphics_context_.GetDevice()));
    res_manager_.RegisterLoader<tryengine::graphics::Material>(tryengine::graphics::MaterialLoader(res_manager_));
}

bool Editor::LoadGameLibrary(const std::string& originalPath) {
    UnloadGameLibrary();

    namespace fs = std::filesystem;
    const std::string tempPath = originalPath + "_temp.so";

    try {
        if (fs::exists(originalPath)) {
            fs::copy_file(originalPath, tempPath, fs::copy_options::overwrite_existing);
        } else {
            std::cerr << "[Editor] Ошибка: Файл библиотеки не найден: " << originalPath << '\n';
            return false;
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "[Editor] Ошибка копирования библиотеки: " << e.what() << '\n';
        return false;
    }

    gameSO.handle = dlopen(tempPath.c_str(), RTLD_NOW);
    if (!gameSO.handle) {
        std::cerr << "[Editor] Ошибка dlopen: " << dlerror() << '\n';
        return false;
    }

    gameSO.updateGameSystems = reinterpret_cast<UpdateGameSystemsFn>(dlsym(gameSO.handle, "UpdateGameSystems"));

    const char* err = dlerror();
    if (err || !gameSO.IsValid()) {
        std::cerr << "[Editor] Ошибка dlsym: " << (err ? err : "Символы не найдены") << '\n';
        UnloadGameLibrary();
        return false;
    }

    std::cout << "[Editor] Библиотека игры успешно загружена!\n";
    return true;
}

void Editor::UnloadGameLibrary() {
    if (gameSO.handle) {
        dlclose(gameSO.handle);
        gameSO.handle = nullptr;
        gameSO.updateGameSystems = nullptr;
        std::cout << "[Editor] Библиотека игры выгружена.\n";
    }
}

void Editor::LoadDefaultScene() const {
    engine_.GetSceneManager().LoadScene("default_scene");
    auto& registry = engine_.GetSceneManager().GetActiveScene()->GetRegistry();

    const auto editor_camera = registry.create();
    registry.emplace<tryengine::Tag>(editor_camera, "EditorCamera");
    registry.emplace<tryengine::Transform>(
        editor_camera, tryengine::Transform{glm::vec3(0.f, 0.f, 10.f), glm::quat(), glm::vec3(1.f)});
    registry.emplace<tryengine::Camera>(editor_camera);
    registry.emplace<EditorCameraTag>(editor_camera);
    registry.emplace<tryengine::Relationship>(editor_camera);

    const auto game_camera = registry.create();
    registry.emplace<tryengine::Tag>(game_camera, "GameCamera");
    registry.emplace<tryengine::Transform>(
        game_camera, tryengine::Transform{glm::vec3(0.f, 2.f, 10.f), glm::quat(), glm::vec3(1.f)});
    registry.emplace<tryengine::Camera>(game_camera);
    registry.emplace<tryengine::MainCameraTag>(game_camera);
    registry.emplace<tryengine::Relationship>(game_camera);
}

}  // namespace tryeditor