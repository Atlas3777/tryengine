#pragma once

#include <memory>

#include "PlayModeState.hpp"
#include "engine/core/Engine.hpp"
#include "engine/graphics/GraphicsContext.hpp"
#include "engine/graphics/VulkanDevice.hpp"

namespace tryeditor {

class AssetPipeline;
class AssetInspectorManager;
class AssetsFactoryManager;
class ImportSystem;

class Editor {
public:
    Editor(tryengine::core::Engine& engine, tryengine::graphics::VulkanDevice& vulkan_device);
    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;
    Editor(Editor&&) noexcept = delete;
    Editor& operator=(Editor&&) noexcept = delete;

    ~Editor();

    void RegisterResourceLoaders() const;
    void RegisterAssetsImporters() const;
    void RegisterAssetsFactories() const;
    void RegisterAssetsInspector() const;

    bool running = false;
    PlayModeState state;

    [[nodiscard]] AssetPipeline& GetAssetSourceDatabase() const { return *asset_pipeline_; }

private:
    tryengine::core::Engine& engine_;
    tryengine::graphics::VulkanDevice& vulkan_device_;

    std::unique_ptr<AssetPipeline> asset_pipeline_;
    std::unique_ptr<AssetsFactoryManager> assets_factory_;
    std::unique_ptr<AssetInspectorManager> asset_inspector_manager_;
};
}  // namespace tryeditor
