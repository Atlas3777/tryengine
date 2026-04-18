#pragma once
#include <unordered_map>
#include <memory>
#include <string>
#include <filesystem>
#include <imgui.h>
#include "IAssetInspector.hpp"

namespace tryeditor {

class AssetInspectorManager {
public:
    // Ключ - это importer_type (например, "ShaderImporter")
    template <typename T, typename... Args>
    void RegisterInspector(const std::string& importer_type, Args&&... args) {
        inspectors_[importer_type] = std::make_unique<T>(std::forward<Args>(args)...);
    }

    // FileBrowser или окно инспектора должны передавать сюда тип, который они прочитали из AssetHeader
    void Draw(const std::filesystem::path& asset_path, const std::string& importer_type) {
        auto it = inspectors_.find(importer_type);
        if (it != inspectors_.end()) {
            it->second->DrawInspector(asset_path);
        } else {
            ImGui::TextDisabled("No inspector registered for type: %s", importer_type.c_str());
        }
    }

private:
    std::unordered_map<std::string, std::unique_ptr<IAssetInspector>> inspectors_;
};

} // namespace tryeditor