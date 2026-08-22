#include "editor/Editor.hpp"

#include "editor/AssetSourceDatabase.hpp"
#include "editor/EditorArtifactLocator.hpp"
#include "editor/asset_factories/AssetsFactoryManager.hpp"
#include "editor/asset_factories/MaterialAssetFactory.hpp"
#include "editor/asset_factories/ShaderAssetFactory.hpp"
#include "editor/asset_inspector/AssetInspectorManager.hpp"
#include "editor/asset_inspector/MaterialAssetInspector.hpp"
#include "editor/asset_inspector/ShaderAssetInspector.hpp"
#include "editor/asset_inspector/TextureAssetInspector.hpp"
#include "editor/gui/EditorGUI.hpp"
#include "editor/import/GlslImporter.hpp"
#include "editor/import/GltfImporter.hpp"
#include "editor/import/SlangImporter.hpp"
#include "editor/meta/ModelAssetMap.hpp"
#include "engine/graphics/loaders/JsonLoader.hpp"
#include "engine/graphics/loaders/MaterialLoader.hpp"
#include "engine/graphics/loaders/MeshLoader.hpp"
#include "engine/graphics/loaders/ShaderLoader.hpp"
#include "engine/graphics/loaders/TextureLoader.hpp"
#include "engine/resources/AssetRegistry.hpp"
#include "engine/resources/AssetTypes.hpp"
#include "engine/resources/ResourceManager.hpp"

namespace tryeditor {

Editor::~Editor() = default;

Editor::Editor(tryengine::core::Engine& engine, tryengine::graphics::GraphicsContext& graphics_context)
    : graphics_context_(graphics_context), engine_(engine) {
    target = std::make_unique<tryengine::graphics::RenderTarget>(graphics_context_.GetDevice(), 1066/1.5f, 600/1.5f, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
    asset_inspector_manager_ = std::make_unique<AssetInspectorManager>();

    assets_factory_ = std::make_unique<AssetsFactoryManager>();
    import_system_ = std::make_unique<ImportSystem>();
    asset_source_database_ =
        std::make_unique<AssetSourceDatabase>(*import_system_, engine_.Get<tryengine::resources::AssetRegistry>());

    gui_ = std::make_unique<EditorGUI>(engine_, graphics_context_);

    RegisterAssetsImporters();
    RegisterResourceLoaders();
    RegisterAssetsFactories();
    RegisterAssetsInspector();
}

void Editor::RegisterAssetsFactories() const {
    // assets_factory_->RegisterFactory<ShaderAssetFactory>(*import_system_);
    // assets_factory_->RegisterFactory<MaterialAssetFactory>(*import_system_);
    // assets_factory_->RegisterFactory<SceneAssetFactory>(*import_system_,
    //                                                     engine_.Get<tryengine::core::ComponentRegistry>());
    // assets_factory_->RegisterFactory<AddressablesGroupFactory>(*import_system_);
    // assets_factory_->RegisterFactory<AddressablesManifestFactory>(*import_system_);
}

void Editor::RegisterAssetsInspector() const {
    // asset_inspector_manager_->RegisterInspector<ShaderAssetInspector>("shader", *import_system_);
    // asset_inspector_manager_->RegisterInspector<TextureAssetInspector>("texture", *import_system_);
    // asset_inspector_manager_->RegisterInspector<MaterialAssetInspector>("material", *import_system_);
}

void Editor::RegisterAssetsImporters() const {
    // import_system_->RegisterImporter<NativeImporter, EmptySettings>({".scene", ".mat", ".prefab"}, *assets_factory_);
    import_system_->RegisterImporter<GlslImporter, GlslShaderImportSettings>({".vert", ".frag"});
    import_system_->RegisterImporter<GltfImporter, GltfImportSettings>({".glb", ".gltf"});
    import_system_->RegisterImporter<SlangImporter, SlangImportSettings>({".slang"});
    // import_system_->RegisterImporter<TextureImporter, TextureImportSettings>({".png", ".jpg"});
}

void Editor::RegisterResourceLoaders() const {
    auto& res_manager = engine_.Get<tryengine::resources::ResourceManager>();

    res_manager.RegisterTypeWithLocator<ModelAssetMap>(
        JsonLoader<ModelAssetMap>(),
        EditorArtifactLocator{"game/artifacts", static_cast<uint32_t>(tryengine::EditorArtifactGuid::AssetMap)});

    res_manager.RegisterType<tryengine::graphics::Material>(tryengine::graphics::MaterialLoader(res_manager));
    res_manager.RegisterType<tryengine::graphics::Mesh>(tryengine::graphics::MeshLoader(graphics_context_.GetDevice()));
    res_manager.RegisterType<tryengine::graphics::Shader>(
        tryengine::graphics::ShaderLoader(res_manager, graphics_context_.GetDevice()));
    res_manager.RegisterType<tryengine::graphics::TextureSampler>(
            tryengine::graphics::TextureLoader(graphics_context_.GetDevice()));

    // res_manager.RegisterType<tryengine::resources::MeshData>(tryengine::resources::MeshDataLoader(res_manager_));
    // res_manager.RegisterLoader<tryengine::graphics::Mesh>(
    //     tryengine::graphics::MeshLoader(res_manager_, graphics_context_.GetDevice()));
    //
    // res_manager.RegisterLoader<tryengine::graphics::Texture>(
    //     tryengine::graphics::TextureLoader(res_manager_, graphics_context_.GetDevice()));

    // res_manager.RegisterLoader<tryengine::graphics::Material>(tryengine::graphics::MaterialLoader(res_manager_));
}

void Editor::LoadDefaultScene() const {
    // auto scene = std::make_unique<tryengine::core::Scene>("nameless_scene");
    //
    // auto& registry = scene->GetRegistry();
    //
    // const auto game_camera = registry.create();
    // registry.emplace<tryengine::Tag>(game_camera, "GameCamera");
    // registry.emplace<tryengine::Transform>(
    //     game_camera, tryengine::Transform{glm::vec3(0.f, 2.f, 10.f), glm::quat(), glm::vec3(1.f)});
    // registry.emplace<tryengine::Camera>(game_camera);
    // registry.emplace<tryengine::MainCameraTag>(game_camera);
    // registry.emplace<tryengine::Relationship>(game_camera);
    //
    // const auto editor_camera = registry.create();
    // registry.emplace<tryengine::Tag>(editor_camera, "EditorCamera");
    // registry.emplace<tryengine::Transform>(
    //     editor_camera, tryengine::Transform{glm::vec3(0.f, 0.f, 10.f), glm::quat(), glm::vec3(1.f)});
    // registry.emplace<tryengine::Camera>(editor_camera);
    // registry.emplace<EditorCameraTag>(editor_camera);
    // registry.emplace<tryengine::Relationship>(editor_camera);
    //
    // // Основной белый источник света (Сверху-справа)
    // const auto main_light = registry.create();
    // registry.emplace<tryengine::Tag>(main_light, "Main_PointLight");
    // registry.emplace<tryengine::Transform>(
    //     main_light, tryengine::Transform{glm::vec3(10.f, 5.f, 0.f), glm::quat(), glm::vec3(1.f)});
    // registry.emplace<tryengine::LightComponent>(main_light,
    //                                             tryengine::LightComponent{glm::vec3(1.0f, 0.95f, 0.9f), 1.2f, 20.0f});
    // registry.emplace<tryengine::Relationship>(main_light);
    //
    // // Дополнительный заполняющий синий источник (Слева)
    // const auto fill_light = registry.create();
    // registry.emplace<tryengine::Tag>(fill_light, "Fill_BlueLight");
    // registry.emplace<tryengine::Transform>(
    //     fill_light, tryengine::Transform{glm::vec3(-5.f, 3.f, 0.f), glm::quat(), glm::vec3(1.f)});
    // registry.emplace<tryengine::LightComponent>(fill_light,
    //                                             tryengine::LightComponent{glm::vec3(0.2f, 0.5f, 1.0f), 1.0f, 15.0f});
    // registry.emplace<tryengine::Relationship>(fill_light);
    //
    // engine_.Get<tryengine::core::SceneManager>().SetActiveScene(std::move(scene));
}

}  // namespace tryeditor