#include "editor/Editor.hpp"

#include <filesystem>

#include "engine/graphics/loaders/MeshLoader.hpp"
#include "editor/AddressablesProvider.hpp"
#include "editor/Components.hpp"
#include "editor/ControllerManager.hpp"
#include "editor/ReflectionSystem.hpp"
#include "editor/SceneManagerController.hpp"
#include "editor/SelectionManager.hpp"
#include "editor/Spawner.hpp"
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
#include "editor/import/NativeImporter.hpp"
#include "engine/core/Addressables.hpp"
#include "engine/core/ComponentSerializers.hpp"
#include "engine/core/Components.hpp"
#include "engine/core/GameAPI.hpp"
#include "engine/core/ResourceManager.hpp"
#include "engine/core/SceneManager.hpp"
#include "engine/graphics/Types.hpp"
#include "engine/graphics/loaders/MaterialLoader.hpp"
#include "engine/graphics/loaders/ShaderAssetLoader.hpp"
#include "engine/resources/MeshDataLoader.hpp"
#include "engine/resources/TextureLoader.hpp"
#include "engine/resources/Types.hpp"

namespace tryeditor {

Editor::Editor(tryengine::core::Engine& engine, tryengine::graphics::GraphicsContext& graphics_context)
    : graphics_context_(graphics_context), engine_(engine) {
    selection_manager_ = std::make_unique<SelectionManager>();
    asset_inspector_manager_ = std::make_unique<AssetInspectorManager>();

    assets_factory_ = std::make_unique<AssetsFactoryManager>();
    import_system_ = std::make_unique<ImportSystem>(engine_.Get<tryengine::core::ResourceManager>());
    addressables_provider_ = std::make_unique<AddressablesProvider>(engine_.Get<tryengine::core::ResourceManager>().GetAddressables());

    spawner_ = std::make_unique<Spawner>(graphics_context_, engine_.Get<tryengine::core::ResourceManager>(), *import_system_);
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

    gui_controller_manager_->RegisterController<SceneManagerController>(
        engine_.Get<tryengine::core::SceneManager>(), *import_system_, *assets_factory_->GetFactory<SceneAssetFactory>(),
        *addressables_provider_);

    GetImportSystem().Refresh();

    engine_.Get<tryengine::core::ResourceManager>().GetAssetDatabase().Refresh();

    LoadGameLibrary("build/game/libgame.so");
    LoadDefaultScene();

    reflection_system_->RegisterBase();
}

Editor::~Editor() {
    UnloadGameLibrary();
}

void Editor::RegisterComponents() const {
    auto reg = engine_.Get<tryengine::core::ComponentRegistry>();
    reg.Register<tryengine::Tag>("Tag");
    reg.Register<tryengine::Transform>("Transform");
    reg.Register<tryengine::Relationship>("Relationship");
    reg.Register<tryengine::Camera>("Camera");
    reg.Register<tryengine::MainCameraTag>("MainCameraTag");
    reg.Register<EditorCameraTag>("EditorCameraTag");
    reg.Register<tryengine::MeshFilter>("MeshFilter");
    reg.Register<tryengine::MeshRenderer>("MeshRenderer");
}

void Editor::RegisterAssetsFactories() const {
    assets_factory_->RegisterFactory<ShaderAssetFactory>(*import_system_);
    assets_factory_->RegisterFactory<MaterialAssetFactory>(*import_system_);
    assets_factory_->RegisterFactory<SceneAssetFactory>(*import_system_, engine_.Get<tryengine::core::ComponentRegistry>());
    // assets_factory_->RegisterFactory<AddressablesGroupFactory>(*import_system_);
    // assets_factory_->RegisterFactory<AddressablesManifestFactory>(*import_system_);
}

void Editor::RegisterAssetsInspector() const {
    asset_inspector_manager_->RegisterInspector<ShaderAssetInspector>("shader", *import_system_);
    asset_inspector_manager_->RegisterInspector<TextureAssetInspector>("texture", *import_system_);
    asset_inspector_manager_->RegisterInspector<MaterialAssetInspector>("material", *import_system_);
}

void Editor::RegisterAssetsImporters() const {
    import_system_->RegisterImporter<NativeImporter, EmptySettings>({".scene", ".mat", ".prefab"}, *assets_factory_);
    import_system_->RegisterImporter<GlslShaderImporter, GlslShaderImportSettings>({".vert", ".frag"});
    import_system_->RegisterImporter<GltfImporter, GltfImportSettings>({".glb", ".gltf"}, *assets_factory_,
                                                                       *import_system_);
    // import_system_->RegisterImporter<TextureImporter, TextureImportSettings>({".png", ".jpg"});
}

void Editor::RegisterResourceLoaders() const {
    auto& res_manager_ = engine_.Get<tryengine::core::ResourceManager>();
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
    return true;
}

void Editor::UnloadGameLibrary() {
}

void Editor::LoadDefaultScene() const {
    auto scene = std::make_unique<tryengine::core::Scene>("nameless_scene");

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

    // Основной белый источник света (Сверху-справа)
    const auto main_light = registry.create();
    registry.emplace<tryengine::Tag>(main_light, "Main_PointLight");
    registry.emplace<tryengine::Transform>(
        main_light, tryengine::Transform{glm::vec3(4.f, 6.f, 3.f), glm::quat(), glm::vec3(1.f)});
    registry.emplace<tryengine::LightComponent>(
        main_light, tryengine::LightComponent{glm::vec3(1.0f, 0.95f, 0.9f), 1.2f, 20.0f}); // Теплый белый, радиус 20
    registry.emplace<tryengine::Relationship>(main_light);

    // Дополнительный заполняющий синий источник (Слева)
    const auto fill_light = registry.create();
    registry.emplace<tryengine::Tag>(fill_light, "Fill_BlueLight");
    registry.emplace<tryengine::Transform>(
        fill_light, tryengine::Transform{glm::vec3(-5.f, 3.f, 2.f), glm::quat(), glm::vec3(1.f)});
    registry.emplace<tryengine::LightComponent>(
        fill_light, tryengine::LightComponent{glm::vec3(0.2f, 0.5f, 1.0f), 1.5f, 15.0f}); // Яркий синий, радиус 15
    registry.emplace<tryengine::Relationship>(fill_light);

    engine_.Get<tryengine::core::SceneManager>().SetActiveScene(std::move(scene));
}

}  // namespace tryeditor
