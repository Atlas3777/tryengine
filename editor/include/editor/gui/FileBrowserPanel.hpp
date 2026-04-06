#pragma once
#include <imgui.h>

#include <filesystem>

#include "engine/core/AssetDatabase.hpp"
#include "editor/gui/IPanel.hpp"
#include "editor/import/ImportSystem.hpp"

namespace editor {

class FileBrowserPanel : public IPanel {
public:
    FileBrowserPanel(ImportSystem& import_system);

    const char* GetName() const override { return "FileBrowserPanel"; }

    void OnImGuiRender(entt::registry& reg) override;

private:
    ImportSystem& import_system_;
    void DrawDirectoryTree(const std::filesystem::path& directoryPath);
    void DrawDirectoryContent();

    std::filesystem::path root_directory_;
    std::filesystem::path selected_directory_;
};

} // namespace editor