#pragma once
#include <entt/entity/entity.hpp>
#include <filesystem>
#include <utility>
namespace tryeditor{
struct EditorContext {
    void SetActive(std::filesystem::path file) {
        selected_asset_path = std::move(file);
    };

    std::filesystem::path selected_asset_path;
};
}