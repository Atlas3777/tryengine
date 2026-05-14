#include <dlfcn.h>
#include <editor/Editor.hpp>
#include <filesystem>
#include <iostream>

#include "editor/AddressablesProvider.hpp"
#include "editor/Components.hpp"
#include "editor/ControllerManager.hpp"
#include "editor/ReflectionSystem.hpp"
#include "editor/SceneManagerController.hpp"
#include "editor/SelectionManager.hpp"
#include "editor/Spawner.hpp"
#include "editor/asset_factories/AddressablesGroupFactory.hpp"
#include "editor/asset_factories/AddressablesManifestFactory.hpp"
#include "editor/asset_factories/AssetsFactoryManager.hpp"
#include "editor/asset_factories/MaterialAssetFactory.hpp"
#include "editor/asset_factories/SceneAssetFactory.hpp"
#include "editor/asset_factories/ShaderAssetFactory.hpp"
#include "editor/asset_inspector/AssetInspectorManager.hpp"
#include "editor/asset_inspector/MaterialAssetInspector.hpp"
#include "editor/asset_inspector/ShaderAssetInspector.hpp"
#include "editor/asset_inspector/TextureAssetInspector.hpp"
#include "editor/gui/EditorGUI.hpp"
#include "editor/import/GlslShaderImporter.hpp"
#include "editor/import/GltfImporter.hpp"
#include "engine/core/Addressables.hpp"
#include "engine/core/ComponentSerializers.hpp"
#include "engine/core/Components.hpp"
#include "engine/core/GameAPI.hpp"
#include "engine/core/ResourceManager.hpp"
#include "engine/core/SceneManager.hpp"
#include "engine/graphics/MaterialLoader.hpp"
#include "engine/graphics/MeshLoader.hpp"
#include "engine/graphics/ShaderAssetLoader.hpp"
#include "engine/graphics/Types.hpp"
#include "engine/resources/MeshDataLoader.hpp"
#include "engine/resources/TextureLoader.hpp"
#include "engine/resources/Types.hpp"

namespace tryeditor {

Editor::Editor(tryengine::core::Engine& engine, tryengine::graphics::GraphicsContext& graphics_context)
    : graphics_context_(graphics_context), engine_(engine) {
    selection_manager_ = std::make_unique<SelectionManager>();
    asset_inspector_manager_ = std::make_unique<AssetInspectorManager>();

    assets_factory_ = std::make_unique<AssetsFactoryManager>();
    import_system_ = std::make_unique<ImportSystem>();
    addressables_provider_ = std::make_unique<AddressablesProvider>(engine_.GetResourceManager().GetAddressables());

    spawner_ = std::make_unique<Spawner>(graphics_context_, engine.GetResourceManager(), *import_system_);
    reflection_system_ = std::make_unique<ReflectionSystem>();
    gui_controller_manager_ = std::make_unique<ControllerManager>();

    editor_gui_ = std::make_unique<EditorGUI>(engine_, graphics_context_, *import_system_, *spawner_,
                                              *selection_manager_, *assets_factory_, *asset_inspector_manager_,
                                              *addressables_provider_, *gui_controller_manager_);
}

void Editor::Init() {
    RegisterComponents();
    RegisterAssetsImporters();
    RegisterResourceLoaders();
    RegisterAssetsFactories();
    RegisterAssetsInspector();

    gui_controller_manager_->RegisterController<SceneManagerController>(engine_.GetSceneManager(), *import_system_,
                                                                       assets_factory_->Get<SceneAssetFactory>(), *addressables_provider_);

    GetImportSystem().Refresh();

    engine_.GetResourceManager().GetAssetDatabase().Refresh();

    LoadGameLibrary("build/game/libgame.so");
    LoadDefaultScene();

    reflection_system_->RegisterBase();
}

Editor::~Editor() {
    UnloadGameLibrary();
}

void Editor::RegisterComponents() const {
    auto& reg = engine_.GetComponentRegistry();
    reg.Register<tryengine::Transform>("Transform");
    reg.Register<tryengine::Tag>("Tag");
    reg.Register<tryengine::Camera>("Camera");
    reg.Register<tryengine::MainCameraTag>("MainCameraTag");
    reg.Register<tryengine::Relationship>("Relationship");
    reg.Register<EditorCameraTag>("EditorCameraTag");
}

void Editor::RegisterAssetsFactories() const {
    assets_factory_->RegisterFactory<ShaderAssetFactory>(*import_system_);
    assets_factory_->RegisterFactory<MaterialAssetFactory>(*import_system_);
    assets_factory_->RegisterFactory<SceneAssetFactory>(*import_system_, engine_.GetComponentRegistry());
    // assets_factory_->RegisterFactory<AddressablesGroupFactory>(*import_system_);
    // assets_factory_->RegisterFactory<AddressablesManifestFactory>(*import_system_);
}

void Editor::RegisterAssetsInspector() const {
    asset_inspector_manager_->RegisterInspector<ShaderAssetInspector>("shader", *import_system_);
    asset_inspector_manager_->RegisterInspector<TextureAssetInspector>("texture", *import_system_);
    asset_inspector_manager_->RegisterInspector<MaterialAssetInspector>("material", *import_system_);
}

void Editor::RegisterAssetsImporters() const {
    import_system_->RegisterImporter<GlslShaderImporter, GlslShaderImportSettings>({".vert", ".frag"});
    import_system_->RegisterImporter<GltfImporter, GltfImportSettings>({".glb", ".gltf"}, *assets_factory_,
                                                                       *import_system_);
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

bool Editor::LoadGameLibrary(const std::string& original_path) {
    UnloadGameLibrary();

    namespace fs = std::filesystem;
    const std::string tempPath = original_path + "_temp.so";

    try {
        if (fs::exists(original_path)) {
            fs::copy_file(original_path, tempPath, fs::copy_options::overwrite_existing);
        } else {
            std::cerr << "[Editor] Library not found: " << original_path << std::endl;
            return false;
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "[Editor] Copy error: " << e.what() << std::endl;
        return false;
    }

    game_lib.handle = dlopen(tempPath.c_str(), RTLD_NOW);
    if (!game_lib.handle) {
        std::cerr << "[Editor] dlopen error: " << dlerror() << std::endl;
        return false;
    }

    game_lib.onInit = (tryengine::core::Game_OnInitFn) dlsym(game_lib.handle, "Game_OnInit");
    game_lib.onUpdate = (tryengine::core::Game_OnUpdateFn) dlsym(game_lib.handle, "Game_OnUpdate");
    game_lib.onShutdown = (tryengine::core::Game_OnShutdownFn) dlsym(game_lib.handle, "Game_OnShutdown");

    if (!game_lib.IsValid()) {
        std::cerr << "[Editor] Failed to find GameAPI functions in " << original_path << std::endl;
        UnloadGameLibrary();
        return false;
    }

    std::cout << "[Editor] Game library loaded. Initializing..." << std::endl;
    game_lib.onInit(engine_);

    return true;
}

void Editor::UnloadGameLibrary() {
    if (game_lib.handle) {
        if (game_lib.onShutdown) {
            game_lib.onShutdown(engine_);
        }

        dlclose(game_lib.handle);
        game_lib.Clear();
        std::cout << "[Editor] Game library unloaded." << std::endl;
    }
}

void Editor::LoadDefaultScene() const {
    // engine_.GetSceneManager().LoadScene("scene1");
    auto scene = std::make_unique<tryengine::core::Scene>();

    auto& registry = scene->GetRegistry();

    const auto game_camera = registry.create();
    registry.emplace<tryengine::Tag>(game_camera, "GameCamera");
    registry.emplace<tryengine::Transform>(
        game_camera, tryengine::Transform{glm::vec3(0.f, 2.f, 10.f), glm::quat(), glm::vec3(1.f)});
    registry.emplace<tryengine::Camera>(game_camera);
    registry.emplace<tryengine::MainCameraTag>(game_camera);
    registry.emplace<tryengine::Relationship>(game_camera);

    const auto editor_camera = registry.create();
    registry.emplace<tryengine::Tag>(editor_camera, "EditorCamera");
    registry.emplace<tryengine::Transform>(
        editor_camera, tryengine::Transform{glm::vec3(0.f, 0.f, 10.f), glm::quat(), glm::vec3(1.f)});
    registry.emplace<tryengine::Camera>(editor_camera);
    registry.emplace<EditorCameraTag>(editor_camera);
    registry.emplace<tryengine::Relationship>(editor_camera);

    engine_.GetSceneManager().SetActiveScene(std::move(scene));
}

}  // namespace tryeditor
