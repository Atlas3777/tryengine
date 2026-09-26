#include "editor/Editor.hpp"

#include "editor/AssetPipeline.hpp"
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
#include "editor/import/ImportSystem.hpp"
#include "editor/import/SlangImporter.hpp"
#include "engine/graphics/loaders/MaterialLoader.hpp"
#include "engine/graphics/loaders/MeshLoader.hpp"
#include "engine/graphics/loaders/ShaderLoader.hpp"
#include "engine/graphics/loaders/TextureLoader.hpp"
#include "engine/platform/InputService.hpp"
#include "engine/resources/AssetRegistry.hpp"
#include "engine/resources/ResourceManager.hpp"

namespace tryeditor {

Editor::~Editor() = default;

Editor::Editor(tryengine::core::Engine& engine, tryengine::graphics::VulkanDevice& vulkan_device)
    : engine_(engine), vulkan_device_(vulkan_device) {
    asset_inspector_manager_ = std::make_unique<AssetInspectorManager>();

    assets_factory_ = std::make_unique<AssetsFactoryManager>();
    asset_pipeline_ = std::make_unique<AssetPipeline>(engine_.Get<tryengine::resources::AssetRegistry>());

    RegisterAssetsImporters();
    RegisterResourceLoaders();
    RegisterAssetsFactories();
    RegisterAssetsInspector();
}

void Editor::RegisterAssetsImporters() const {
    auto& import_system_ = asset_pipeline_->GetImportSystem();
    import_system_.RegisterImporter<GlslImporter, GlslShaderImportSettings>({".vert", ".frag"});
    import_system_.RegisterImporter<GltfImporter, GltfImportSettings>({".glb", ".gltf"});
    import_system_.RegisterImporter<SlangImporter, SlangImportSettings>({".slang"});
}

void Editor::RegisterResourceLoaders() const {
    auto& res_manager = engine_.Get<tryengine::resources::ResourceManager>();

    // res_manager.RegisterTypeWithLocator<ModelAssetMap>(
    //     JsonLoader<ModelAssetMap>(),
    //     EditorArtifactLocator{"game/artifacts", static_cast<uint32_t>(tryengine::EditorArtifactGuid::AssetMap)});

    res_manager.RegisterType<tryengine::graphics::Material>(tryengine::graphics::MaterialLoader(vulkan_device_, res_manager));
    res_manager.RegisterType<tryengine::graphics::Mesh>(tryengine::graphics::MeshLoader(vulkan_device_));
    res_manager.RegisterType<tryengine::graphics::Shader>(
        tryengine::graphics::ShaderLoader(vulkan_device_.GetDevice()));
    res_manager.RegisterType<tryengine::graphics::TextureSampler>(
        tryengine::graphics::TextureLoader(vulkan_device_));
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

}  // namespace tryeditor