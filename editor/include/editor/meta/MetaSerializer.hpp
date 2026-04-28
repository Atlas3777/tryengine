#pragma once
#include <cereal/archives/json.hpp>
#include <filesystem>
#include <fstream>
#include <optional>

#include "editor/meta/AssetMetaHeader.hpp"

namespace tryeditor {

class MetaSerializer {
public:
    // Чтение только заголовка (когда настройки не нужны)
    static std::optional<AssetMetaHeader> ReadHeader(const std::filesystem::path& meta_path) {
        std::ifstream is(meta_path);
        if (!is.is_open()) return std::nullopt;
        try {
            cereal::JSONInputArchive archive(is);
            AssetMetaHeader header;
            archive(cereal::make_nvp("header", header));
            return header;
        } catch (...) {
            return std::nullopt;
        }
    }

    // Чтение и заголовка, и специфичных настроек
    template <typename Settings>
    static bool Read(const std::filesystem::path& meta_path, AssetMetaHeader& out_header, Settings& out_settings) {
        std::ifstream is(meta_path);
        if (!is.is_open()) return false;
        try {
            cereal::JSONInputArchive archive(is);
            archive(cereal::make_nvp("header", out_header));
            archive(cereal::make_nvp("settings", out_settings));
            return true;
        } catch (...) {
            return false;
        }
    }

    // Запись заголовка и настроек без перезаписи самого ассета
    template <typename Settings>
    static bool Write(const std::filesystem::path& meta_path, const AssetMetaHeader& header, const Settings& settings) {
        std::ofstream os(meta_path);
        if (!os.is_open()) return false;
        try {
            cereal::JSONOutputArchive archive(os);
            archive(cereal::make_nvp("header", header));
            archive(cereal::make_nvp("settings", settings));
            return true;
        } catch (...) {
            return false;
        }
    }

    // Перезапись только заголовка (если настроек нет, например для шейдеров)
    static bool WriteHeaderOnly(const std::filesystem::path& meta_path, const AssetMetaHeader& header) {
        std::ofstream os(meta_path);
        if (!os.is_open()) return false;
        try {
            cereal::JSONOutputArchive archive(os);
            archive(cereal::make_nvp("header", header));
            return true;
        } catch (...) {
            return false;
        }
    }
};

} // namespace tryeditor