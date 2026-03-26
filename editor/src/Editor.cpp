#include "editor/Editor.hpp"

#include <dlfcn.h>
#include <filesystem>
#include <iostream>

#include "editor/Components.hpp"
#include "engine/core/Components.hpp"
#include "game/GameAPI.hpp"

namespace editor {

Editor::Editor(engine::core::Engine& eng, engine::graphics::GraphicsContext& graphics_context) : engine_(eng) {
    editorGUI = std::make_unique<EditorGUI>(graphics_context);
}

Editor::~Editor() {
    UnloadGameLibrary();  // Обязательно освобождаем память при закрытии
}
bool Editor::LoadGameLibrary(const std::string& originalPath) {
    // 1. Выгружаем предыдущую версию, если она была
    UnloadGameLibrary();

    namespace fs = std::filesystem;
    std::string tempPath = originalPath + "_temp.so";

    // 2. Копируем файл (Hot Reload)
    try {
        if (fs::exists(originalPath)) {
            fs::copy_file(originalPath, tempPath, fs::copy_options::overwrite_existing);
        } else {
            std::cerr << "[Editor] Ошибка: Файл библиотеки не найден: " << originalPath << '\n';
            return false;
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "[Editor] Ошибка копирования библиотеки: " << e.what() << '\n';
        return false;
    }

    // 3. Загружаем временный файл
    gameSO.handle = dlopen(tempPath.c_str(), RTLD_NOW);
    if (!gameSO.handle) {
        std::cerr << "[Editor] Ошибка dlopen: " << dlerror() << '\n';
        return false;
    }

    // 4. Ищем нужные функции
    gameSO.updateGameSystems = reinterpret_cast<UpdateGameSystemsFn>(dlsym(gameSO.handle, "UpdateGameSystems"));

    // Проверка на ошибки dlsym
    const char* err = dlerror();
    if (err || !gameSO.IsValid()) {
        std::cerr << "[Editor] Ошибка dlsym: " << (err ? err : "Символы не найдены") << '\n';
        UnloadGameLibrary();  // Откат, если функции не найдены
        return false;
    }

    std::cout << "[Editor] Библиотека игры успешно загружена!\n";
    return true;
}

void Editor::UnloadGameLibrary() {
    if (gameSO.handle) {
        dlclose(gameSO.handle);
        gameSO.handle = nullptr;
        gameSO.updateGameSystems = nullptr;
        std::cout << "[Editor] Библиотека игры выгружена.\n";
    }
}

void Editor::LoadDefaultScene() {
    engine_.GetSceneManager().LoadScene("default_scene");
    auto& registry = engine_.GetSceneManager().GetActiveScene()->GetRegistry();

    auto editorCamera = registry.create();
    registry.emplace<engine::Tag>(editorCamera, "EditorCamera");
    registry.emplace<engine::Transform>(editorCamera,
    engine::Transform{glm::vec3(0.f, 0.f, -2.f), glm::quat(), glm::vec3(1.f)});
    registry.emplace<engine::Camera>(editorCamera);
    registry.emplace<EditorCameraTag>(editorCamera);



    auto gameCamera = registry.create();
    registry.emplace<engine::Tag>(gameCamera, "GameCamera");
    registry.emplace<engine::Transform>(gameCamera,
    engine::Transform{glm::vec3(0.f, 0.f, -2.f), glm::quat(), glm::vec3(1.f)});
    registry.emplace<engine::Camera>(gameCamera);
    registry.emplace<engine::MainCameraTag>(gameCamera);
}

} // namespace editor