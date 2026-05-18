#pragma once
#include <iostream>

#include "editor/asset_factories/AssetsFactoryManager.hpp"
#include "editor/import/IAssetImporter.hpp"

namespace tryeditor {

class NativeImporter : public ITypedImporter<EmptySettings>, public IAssetImporter {
public:
    NativeImporter(AssetsFactoryManager& factory_manager) : factory_manager_(factory_manager) {}

    [[nodiscard]] std::string GetName() const override { return "NativeImporter"; }
    [[nodiscard]] std::string GetAssetType() const override { return "Native"; }

    bool GenerateArtifact(const AssetContext& context, AssetMetaHeader& header,
                          const EmptySettings& settings) override {
        return true;
    }

    bool Reimport(const AssetContext& context) override {
        AssetMetaHeader header;
        EmptySettings settings;

        if (!MetaSerializer::Read(context.meta_path, header, settings)) return false;

        auto* factory = factory_manager_.GetFactoryByName(header.asset_type);
        if (!factory) {
            std::cerr << "No factory for native asset type: " << header.asset_type << std::endl;
            return false;
        }

        // ТУТ МАГИЯ: Метод создаст папку artifacts/GUID/ и вернет путь к artifacts/GUID/GUID
        auto artifact_file_path = context.EnsureMainArtifactPath(header.guid);

        // Передаем фабрике чистый путь к файлу
        return factory->Cook(context.asset_path, artifact_file_path);
    }

    bool ImportNew(const AssetContext& context, uint64_t new_guid) override {
        // Для Native ассетов ImportNew обычно не вызывается через ImportSystem,
        // так как они создаются через Factory::CreateDefault.
        // Но если подкинуть файл вручную, можно реализовать логику тут.
        return Reimport(context);
    }

private:
    AssetsFactoryManager& factory_manager_;
};

}  // namespace tryeditor