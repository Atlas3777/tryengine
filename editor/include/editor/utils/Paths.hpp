#pragma once
#include <filesystem>

class Paths {
public:
    // Запрещаем создание экземпляра класса
    Paths() = delete;

    // Базовые директории
    static inline const std::filesystem::path root      = std::filesystem::current_path();
    static inline const std::filesystem::path assets    = root / "game" / "assets";
    static inline const std::filesystem::path artifacts    = root / "game" / "artifacts";

    static const inline std::filesystem::path project_data_dir = root / "game" / "project_data";
    
};