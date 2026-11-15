#pragma once

#include <EASTL/string.h>
#include <EASTL/string_view.h>

#include "engine/platform/Directory.hpp"
#include "engine/resources/AssetRegistry.hpp"
#include "engine/resources/PathGuid.hpp"

namespace tryengine::resources {

// Регистрирует свободные файлы каталога (авторитетные ресурсы вроде game/data) под их
// путём: id = PathGuid(путь). У таких файлов нет .meta и импортируемого guid, но кешу
// ресурсов нужен численный id — путь его полностью определяет, поэтому ссылка в сцене
// остаётся стабильной между запусками.
inline void RegisterLooseDirectory(AssetRegistry& registry, eastl::string_view root, eastl::string_view extension) {
    struct Visitor {
        AssetRegistry& registry;
        eastl::string_view extension;

        bool OnDirectoryEnter(eastl::string_view, eastl::string_view) { return true; }
        void OnDirectoryLeave() {}

        void OnFile(eastl::string_view full_path, eastl::string_view name) {
            if (name.size() < extension.size()) {
                return;
            }
            if (name.substr(name.size() - extension.size()) != extension) {
                return;
            }
            registry.RegisterLooseArtifact(PathGuid(full_path), eastl::string(full_path.data(), full_path.size()));
        }
    };

    Visitor visitor{registry, extension};
    filesystem::Walk(root, visitor);
}

}  // namespace tryengine::resources
