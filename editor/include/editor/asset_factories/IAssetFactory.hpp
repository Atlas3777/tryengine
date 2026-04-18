#pragma once
#include <filesystem>
#include <string>

namespace tryeditor {

class IAssetFactory {
public:
    virtual ~IAssetFactory() = default;

    // Вызывается из FileBrowser (GUI) с параметрами по умолчанию
    virtual uint64_t CreateDefault(const std::filesystem::path& directory) = 0;

    // Имя для отображения в контекстном меню (например, "Shader Asset")
    [[nodiscard]] virtual std::string GetActionName() const = 0;
};

}  // namespace tryeditor