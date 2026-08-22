#pragma once

#include <EASTL/span.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>

#include "editor/import/IAssetImporter.hpp"
#include "engine/core/Engine.hpp"

namespace tryeditor {

class ImportSystem {
public:
    ImportSystem() = default;
    ~ImportSystem() = default;

    template <typename TImporter, typename TSettings, typename... Args>
    void RegisterImporter(eastl::span<const eastl::string_view> extensions, Args&&... args) {
        auto importer = eastl::make_unique<TImporter>(std::forward<Args>(args)...);

        auto settings_id = tryengine::core::ScopedTypeId<ImportSystem, TSettings>::Value();
        if (settings_id >= importers_by_settings_type_.size()) {
            importers_by_settings_type_.resize(settings_id + 1, nullptr);
        }
        importers_by_settings_type_[settings_id] = importer.get();

        IAssetImporter* ptr = importer.get();
        importers_by_name_[eastl::string(importer->GetName())] = ptr;

        for (const auto& ext : extensions) {
            importers_by_ext_[eastl::string(ext)] = ptr;
        }

        importers_.push_back(eastl::move(importer));
    }

    template <typename TImporter, typename TSettings, typename... Args>
    void RegisterImporter(std::initializer_list<eastl::string_view> extensions, Args&&... args) {
        RegisterImporter<TImporter, TSettings>(
            eastl::span<const eastl::string_view>(extensions.begin(), extensions.end()), std::forward<Args>(args)...);
    }

    template <typename TSettings>
    [[nodiscard]] IAssetImporter* GetImporterBySettings() const {
        auto settings_id = tryengine::core::ScopedTypeId<ImportSystem, TSettings>::Value();
        if (settings_id < importers_by_settings_type_.size()) {
            return importers_by_settings_type_[settings_id];
        }
        return nullptr;
    }

    [[nodiscard]] IAssetImporter* GetImporterByName(eastl::string_view name) const;
    [[nodiscard]] IAssetImporter* GetImporterByExtension(eastl::string_view ext) const;

private:
    eastl::vector<eastl::unique_ptr<IAssetImporter>> importers_;

    eastl::vector<IAssetImporter*> importers_by_settings_type_;

    eastl::unordered_map<eastl::string, IAssetImporter*> importers_by_ext_;
    eastl::unordered_map<eastl::string, IAssetImporter*> importers_by_name_;
};

}  // namespace tryeditor