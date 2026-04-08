#include "editor/Editor.hpp"

#include <dlfcn.h>
#include <filesystem>
#include <iostream>

#include "editor/Components.hpp"
#include "editor/Spawner.hpp"
#include "editor/import/GltfImporter.hpp"
#include "editor/import/ImageImporter.hpp"
#include "engine/core/Components.hpp"
#include "engine/core/ResourceManager.hpp"
#include "engine/graphics/GpuMeshLoader.hpp"
#include "engine/graphics/Types.hpp"
#include "engine/resources/TMeshLoader.hpp"
#include "engine/resources/Types.hpp"
#include "game/GameAPI.hpp"

namespace tryeditor {

Editor::Editor(tryengine::core::Engine& eng, tryengine::graphics::GraphicsContext& graphics_context,
               tryengine::graphics::RenderSystem& render_system)
    : graphics_context_(graphics_context), engine_(eng) {
    import_system_ = std::make_unique<ImportSystem>();
    spawner_ = std::make_unique<Spawner>(graphics_context, eng.GetResourceManager(), render_system, *import_system_);
    editor_gui_ = std::make_unique<EditorGUI>(graphics_context, *import_system_, *spawner_);
}

Editor::~Editor() {
    UnloadGameLibrary();
}
void Editor::RegisterAssetsImporters() const {
    import_system_->RegisterImporter<GltfImporter>(".glb");
    import_system_->RegisterImporter<ImageImporter>(".png");
}
void Editor::RegisterResourceLoaders() const {
    auto& res_manager_ = engine_.GetResourceManager();
    res_manager_.RegisterLoader<tryengine::resources::MeshData>(tryengine::resources::TMeshLoader(res_manager_));
    res_manager_.RegisterLoader<tryengine::graphics::Mesh>(
        tryengine::graphics::GpuMeshLoader(res_manager_, graphics_context_.GetDevice()));
}

bool Editor::LoadGameLibrary(const std::string& originalPath) {
    UnloadGameLibrary();

    namespace fs = std::filesystem;
    const std::string tempPath = originalPath + "_temp.so";

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
        UnloadGameLibrary();
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

void Editor::LoadDefaultScene() const {
    engine_.GetSceneManager().LoadScene("default_scene");
    auto& registry = engine_.GetSceneManager().GetActiveScene()->GetRegistry();

    const auto editorCamera = registry.create();
    registry.emplace<tryengine::Tag>(editorCamera, "EditorCamera");
    registry.emplace<tryengine::Transform>(
        editorCamera, tryengine::Transform{glm::vec3(0.f, 0.f, 10.f), glm::quat(), glm::vec3(1.f)});  // Позиция Z = 10
    registry.emplace<tryengine::Camera>(editorCamera);
    registry.emplace<EditorCameraTag>(editorCamera);

    const auto gameCamera = registry.create();
    registry.emplace<tryengine::Tag>(gameCamera, "GameCamera");
    registry.emplace<tryengine::Transform>(gameCamera,
                                        tryengine::Transform{glm::vec3(0.f, 2.f, 10.f), glm::quat(), glm::vec3(1.f)});
    registry.emplace<tryengine::Camera>(gameCamera);
    registry.emplace<tryengine::MainCameraTag>(gameCamera);
}

}  // namespace tryeditor