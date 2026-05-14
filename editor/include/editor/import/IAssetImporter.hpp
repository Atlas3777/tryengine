#pragma once
#include <cereal/archives/binary.hpp>
#include <filesystem>
#include <fstream>
#include <string>

#include "editor/AssetContext.hpp"
#include "editor/meta/AssetMetaHeader.hpp"
#include "editor/meta/MetaSerializer.hpp"

namespace tryeditor {

class IAssetImporter {
public:
    virtual ~IAssetImporter() = default;

    [[nodiscard]] virtual std::string GetName() const = 0;
    [[nodiscard]] virtual std::string GetAssetType() const = 0;

    virtual bool Reimport(const AssetContext& context) = 0;
    virtual bool ImportNew(const AssetContext& context, uint64_t new_guid) = 0;
};

template <typename TSettings>
class ITypedImporter {
public:
    virtual ~ITypedImporter() = default;

    virtual bool GenerateArtifact(const AssetContext& context, AssetMetaHeader& header, const TSettings& settings) = 0;
};

template <typename TSettings>
class BaseTypedImporter : public IAssetImporter, public ITypedImporter<TSettings> {
public:
    bool Reimport(const AssetContext& context) override {
        TSettings settings{};
        AssetMetaHeader header;

        // Читаем старую мету. Если файла нет или он бит, реимпорт не делаем.
        if (!MetaSerializer::Read(context.meta_path, header, settings)) {
            return false;
        }

        // Передаем header со старым guid дальше
        return this->GenerateArtifact(context, header, settings);
    }

    bool ImportNew(const AssetContext& context, uint64_t new_guid) override {
        TSettings settings{};
        AssetMetaHeader header;

        header.guid = new_guid;
        header.importer_type = this->GetName();
        header.asset_type = this->GetAssetType();

        MetaSerializer::Write(context.meta_path, header, settings);

        // Передаем header с новым guid дальше
        return this->GenerateArtifact(context, header, settings);
    }
};

template <typename T, typename TSettings>
concept AssetImporter = std::default_initializable<TSettings> && std::derived_from<T, IAssetImporter> &&
                        std::derived_from<T, ITypedImporter<TSettings>>;

}  // namespace tryeditor