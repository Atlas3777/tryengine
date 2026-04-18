#pragma once
#include <filesystem>

namespace tryeditor {

class IAssetInspector {
public:
    virtual ~IAssetInspector() = default;

    // asset_path - путь к оригинальному файлу исходника (ftm.glb или NewShader.shader)
    virtual void DrawInspector(const std::filesystem::path& asset_path) = 0;
};

} // namespace tryeditor