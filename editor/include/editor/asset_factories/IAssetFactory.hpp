#pragma once
#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <filesystem>
#include <fstream>
#include <string>

#include "editor/meta/AssetMetaHeader.hpp"
#include "editor/meta/MetaSerializer.hpp"
#include "engine/core/RandomUtil.hpp"

namespace tryeditor {

class IAssetFactory {
public:
    virtual ~IAssetFactory() = default;

    virtual uint64_t CreateDefault(const std::filesystem::path& directory) = 0;

    virtual bool Cook(const std::filesystem::path& asset_path, const std::filesystem::path& artifact_path) = 0;

    [[nodiscard]] virtual std::string GetActionName() const = 0;
    [[nodiscard]] virtual std::string GetAssetType() const = 0;
};

// Пустые настройки, так как у нативных ассетов настройки внутри файла
struct EmptySettings {
    template <class Archive>
    void serialize(Archive&) {}
};

template <typename TAsset>
class BaseAssetFactory : public IAssetFactory {
public:
    uint64_t Create(const TAsset& asset, const std::string& name, const std::filesystem::path& directory) {
        std::filesystem::path asset_path = directory / (name + GetExtension());

        // Авто-инкремент имени, если файл существует
        for (int i = 1; std::filesystem::exists(asset_path); ++i) {
            asset_path = directory / (name + "_" + std::to_string(i) + GetExtension());
        }

        SaveSource(asset_path, asset);

        // 2. Создаем мету
        AssetMetaHeader header;
        header.guid = tryengine::core::RandomUtil::GenerateInt64();
        header.importer_type = "NativeImporter";
        header.asset_type = GetAssetType();

        MetaSerializer::Write(asset_path.string() + ".meta", header, EmptySettings{});

        return header.guid;
    }

    uint64_t CreateDefault(const std::filesystem::path& directory) override {
        std::string name = GetDefaultName();
        std::filesystem::path asset_path = directory / (name + GetExtension());

        // Авто-инкремент имени, если файл существует
        for (int i = 1; std::filesystem::exists(asset_path); ++i) {
            asset_path = directory / (name + "_" + std::to_string(i) + GetExtension());
        }

        TAsset default_asset = CreateDefaultAsset();
        SaveSource(asset_path, default_asset);

        // 2. Создаем мету
        AssetMetaHeader header;
        header.guid = tryengine::core::RandomUtil::GenerateInt64();
        header.importer_type = "NativeImporter";
        header.asset_type = GetAssetType();

        MetaSerializer::Write(asset_path.string() + ".meta", header, EmptySettings{});

        return header.guid;
    }

    // Реализация компиляции: загружаем исходник (JSON) -> сохраняем артефакт (Binary)
    bool Cook(const std::filesystem::path& asset_path, const std::filesystem::path& artifact_path) override {
        TAsset asset;
        if (!LoadSource(asset_path, asset))
            return false;

        return SaveArtifact(artifact_path, asset);
    }


protected:
    [[nodiscard]] virtual std::string GetExtension() const = 0;
    [[nodiscard]] virtual std::string GetDefaultName() const = 0;
    [[nodiscard]] virtual TAsset CreateDefaultAsset() const = 0;

    // Как мы читаем исходник (обычно JSON для удобства гита)
    virtual bool LoadSource(const std::filesystem::path& path, TAsset& out_asset) {
        std::ifstream is(path);
        if (!is.is_open())
            return false;
        cereal::JSONInputArchive ar(is);
        ar(out_asset);
        return true;
    }

    // Как мы пишем исходник
    virtual void SaveSource(const std::filesystem::path& path, const TAsset& asset) {
        std::ofstream os(path);
        cereal::JSONOutputArchive ar(os);
        ar(asset);
    }

    // Как мы пишем артефакт (всегда Binary для скорости рантайма)
    virtual bool SaveArtifact(const std::filesystem::path& path, const TAsset& asset) {
        std::ofstream os(path, std::ios::binary);
        if (!os.is_open())
            return false;
        cereal::BinaryOutputArchive ar(os);
        ar(asset);
        return true;
    }
};

}  // namespace tryeditor