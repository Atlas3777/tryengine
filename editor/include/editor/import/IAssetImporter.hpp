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

    virtual std::string GetName() const = 0;       // Это пойдет в importer_type
    virtual std::string GetAssetType() const = 0;  // Это пойдет в asset_type

    virtual bool Reimport(const AssetContext& context) = 0;
    virtual bool ImportNew(const AssetContext& context, uint64_t new_guid) = 0;
};

template <typename TSettings>
class ITypedImporter {
public:
    virtual ~ITypedImporter() = default;
    virtual bool GenerateArtifact(const AssetContext& context, const TSettings& settings) = 0;
};

template <typename TSettings>
class BaseTypedImporter : public IAssetImporter, public ITypedImporter<TSettings> {
public:
    bool Reimport(const AssetContext& context) override {
        TSettings settings{};
        AssetMetaHeader header;
        MetaSerializer::Read(context.meta_path, header, settings);

        return this->GenerateArtifact(context, settings);
    }

    bool ImportNew(const AssetContext& context, uint64_t new_guid) override {
        TSettings settings{};
        AssetMetaHeader header;

        // АВТОМАТИЧЕСКИ ЗАПОЛНЯЕМ МЕТУ ДАННЫМИ ИМПОРТЕРА
        header.guid = new_guid;
        header.importer_type = this->GetName();
        header.asset_type = this->GetAssetType();

        // Пишем на диск
        MetaSerializer::Write(context.meta_path, header, settings);
        return this->GenerateArtifact(context, settings);
    }
};

template <typename T, typename TSettings>
concept AssetImporter = std::default_initializable<TSettings> &&
                        std::derived_from<T, IAssetImporter> &&
                        std::derived_from<T, ITypedImporter<TSettings>>;

}  // namespace tryeditor