#pragma once
#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <entt/core/type_info.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "IAssetImporter.hpp"
#include "editor/AssetContext.hpp"
#include "editor/meta/AssetMetaHeader.hpp"
#include "engine/resources/AssetTypes.hpp"

namespace tryeditor {

class ImportSystem {
public:
    template <typename TImporter, typename TSettings, typename... Args>
        requires AssetImporter<TImporter, TSettings>
    void RegisterImporter(const std::vector<std::string>& extensions, Args&&... args) {
        auto importer = std::make_unique<TImporter>(std::forward<Args>(args)...);

        auto settingsId = entt::type_hash<TSettings>::value();
        importers_by_settings_type_[settingsId] = importer.get();

        std::string name = importer->GetName();
        IAssetImporter* ptr = importer.get();

        importers_.push_back(std::move(importer));
        importers_by_name_[name] = ptr;

        for (const auto& ext : extensions) {
            importers_by_ext_[ext] = ptr;
        }
    }

    template <typename AssetDataType>
    void SaveNativeAsset(const std::filesystem::path& path, AssetDataType asset, AssetMetaHeader& header) {
        auto context = ResolveContext(path);
        {
            std::ofstream os(context.asset_path);
            cereal::JSONOutputArchive archive(os);
            std::cout << entt::type_name<AssetDataType>::value() << std::endl;
            archive(cereal::make_nvp("data", asset));
        }

        {
            std::string asset_guid = std::to_string(header.guid);
            std::filesystem::path artifact_dir = context.artifacts_dir / asset_guid;
            std::filesystem::create_directories(artifact_dir);

            std::ofstream os(artifact_dir / asset_guid, std::ios::binary);
            if (os.is_open()) {
                cereal::BinaryOutputArchive archive(os);
                archive(asset);
            }
        }
    }

    template <typename TSettings>
    void SaveExternalAsset(const AssetContext& context, const AssetMetaHeader& header, const TSettings& settings) {
        {
            std::ofstream os(context.meta_path);
            cereal::BinaryOutputArchive archive(os);
            archive(cereal::make_nvp("header", header));
            archive(cereal::make_nvp("settings", settings));
        }

        auto settingsId = entt::type_hash<TSettings>::value();
        auto it = importers_by_settings_type_.find(settingsId);

        if (it != importers_by_settings_type_.end()) {
            auto* typed = static_cast<ITypedImporter<TSettings>*>(it->second);
            typed->GenerateArtifact(context, settings);
        }
    }

    template <typename AssetData>
    void LoadNativeAsset(const std::filesystem::path& path, AssetData& out_data) {
        {
            std::ifstream is(path);
            cereal::JSONInputArchive archive(is);
            archive(cereal::make_nvp("data", out_data));
        }
    }

    template <typename AssetData>
    AssetData LoadFromCache(const uint64_t id, std::string_view key) {
        auto path = game_cache_dir_;
        path /= std::to_string(id);
        path /= "hierarchy.json";

        AssetData data;
        {
            std::ifstream is(path);
            cereal::JSONInputArchive archive(is);
            archive(cereal::make_nvp(key.data(), data));
        }
        return data;
    }

    template <typename AssetData>
    AssetData LoadProjectData(std::string_view filename) {
        auto path = project_data_dir;
        path /= filename;

        AssetData data;
        {
            std::ifstream is(path);
            cereal::JSONInputArchive archive(is);
            archive( data);
        }
        return data;
    }

    template <typename AssetData>
    void SaveProjectData(AssetData data, std::string_view filename) {
        auto path = project_data_dir;
        path /= filename;

        {
            std::ofstream os(path);
            cereal::JSONOutputArchive archive(os);
            archive(data);
        }
    }

    void Refresh();
    void ImportNewAsset(const AssetContext& ctx, IAssetImporter* importer);
    void ReimportAsset(const AssetMetaHeader& header) const;

    uint64_t GetId(const std::string& path) const { return path_to_id_.at(path); }
    std::string GetPath(const uint64_t id) const { return id_to_path_.at(id); }

    IAssetImporter* GetImporterByName(const std::string& name) const {
        auto it = importers_by_name_.find(name);
        return it != importers_by_name_.end() ? it->second : nullptr;
    }

    AssetContext ResolveContext(const std::filesystem::path& asset_path) const;

    void DeleteAsset(const std::filesystem::path& asset_path);
    void DeleteDirectory(const std::filesystem::path& dir_path);

private:
    void DeleteArtifactsAndCache(uint64_t id);
    void ProcessDirectory(const std::filesystem::path& assets_dir);
    bool ValidateArtifacts(uint64_t guid, const std::filesystem::path& artifacts_dir) const;

    const std::filesystem::path root_path_ = std::filesystem::current_path();
    const std::filesystem::path project_data_dir = root_path_ / "game" / "project_data";

    // Структура папок игры
    const std::filesystem::path game_assets_dir_ = root_path_ / "game" / "assets";
    const std::filesystem::path game_artifacts_dir_ = root_path_ / "game" / "artifacts";
    const std::filesystem::path game_cache_dir_ = root_path_ / "game" / ".cache";

    // Структура папок движка
    const std::filesystem::path engine_assets_dir_ = root_path_ / "engine_content" / "assets";
    const std::filesystem::path engine_artifacts_dir_ = root_path_ / "engine_content" / "artifacts";
    const std::filesystem::path engine_cache_dir_ = root_path_ / "engine_content" / ".cache";

    std::vector<std::unique_ptr<IAssetImporter>> importers_;
    std::unordered_map<std::string, IAssetImporter*> importers_by_ext_;
    std::unordered_map<std::string, IAssetImporter*> importers_by_name_;

    std::unordered_map<entt::id_type, IAssetImporter*> importers_by_settings_type_;

    std::unordered_map<uint64_t, std::string> id_to_path_;
    std::unordered_map<std::string, uint64_t> path_to_id_;
};

}  // namespace tryeditor
