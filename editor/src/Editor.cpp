#include "editor/Editor.hpp"

#include <dlfcn.h>
#include <filesystem>
#include <iostream>

#include "../../engine/core/include/engine/core/ResourceManager.hpp"
#include "editor/Components.hpp"
#include "editor/Spawner.hpp"
#include "editor/import/GltfImporter.hpp"
#include "engine/core/Components.hpp"
#include "engine/graphics/GpuMeshLoader.hpp"
#include "engine/graphics/MaterialSystem.hpp"
#include "engine/graphics/Types.hpp"
#include "engine/resources/TMeshLoader.hpp"
#include "engine/resources/Types.hpp"
#include "game/GameAPI.hpp"

namespace editor {

Editor::Editor(engine::core::Engine& eng, engine::graphics::GraphicsContext& graphics_context, engine::graphics::RenderSystem& render_system) : graphics_context_(graphics_context), engine_(eng)  {
    import_system_ = std::make_unique<ImportSystem>();
    spawner_ = std::make_unique<Spawner>(graphics_context, eng.GetResourceManager(), render_system, *import_system_);
    editor_gui_ = std::make_unique<EditorGUI>(graphics_context, *import_system_, *spawner_);

}

Editor::~Editor() {
    UnloadGameLibrary();  // Обязательно освобождаем память при закрытии
}
void Editor::RegisterAssetsImporters() const {
    import_system_->RegisterImporter<GltfImporter>(".glb");
}
void Editor::RegisterResourceLoaders() const {
    auto& res_manager_ = engine_.GetResourceManager();
    res_manager_.RegisterLoader<engine::resources::MeshData>(
        engine::resources::TMeshLoader(res_manager_));
    res_manager_.RegisterLoader<engine::graphics::Mesh>(
        engine::graphics::GpuMeshLoader(res_manager_, graphics_context_.GetDevice()));
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

void Editor::LoadDefaultScene(engine::graphics::RenderSystem& render_system) {
    engine_.GetSceneManager().LoadScene("default_scene");
    auto& registry = engine_.GetSceneManager().GetActiveScene()->GetRegistry();

    auto editorCamera = registry.create();
    registry.emplace<engine::Tag>(editorCamera, "EditorCamera");
    // Editor.cpp
    registry.emplace<engine::Transform>(editorCamera,
        engine::Transform{glm::vec3(0.f, 0.f, 10.f), glm::quat(), glm::vec3(1.f)}); // Позиция Z = 10
    registry.emplace<engine::Camera>(editorCamera);
    registry.emplace<EditorCameraTag>(editorCamera);

    auto gameCamera = registry.create();
    registry.emplace<engine::Tag>(gameCamera, "GameCamera");
    registry.emplace<engine::Transform>(gameCamera,
                                        engine::Transform{glm::vec3(0.f, 0.f, -2.f), glm::quat(), glm::vec3(1.f)});
    registry.emplace<engine::Camera>(gameCamera);
    registry.emplace<engine::MainCameraTag>(gameCamera);

    // // 1. Создаем глобальный "чертеж" материала (можно хранить в ResourceManager)
    // auto* pbr_material = new engine::graphics::Material();
    // pbr_material->pipeline = render_system.GetRenderer().GetDefaultPipeline(); // Твой текущий пайплайн
    //
    // // Настраиваем Layout под твой PBR шейдер.
    // // ВАЖНО: Слот 0 занят под LightUniforms, поэтому материал пушим в Слот 1.
    // // pbr_material->layout.uniform_binding_slot = 1;
    //
    // // Задаем параметры и их смещения (std140)
    // // vec4 base_color (16 байт) -> offset 0
    // // float roughness (4 байта) -> offset 16
    // // float metallic  (4 байта) -> offset 20
    // // pbr_material->layout.params.push_back({"base_color", engine::graphics::ShaderParamType::Vec4, 0, 16});
    // // pbr_material->layout.params.push_back({"roughness",  engine::graphics::ShaderParamType::Float, 16, 4});
    // // pbr_material->layout.params.push_back({"metallic",   engine::graphics::ShaderParamType::Float, 20, 4});
    //
    // // // Размер должен быть кратен 16 (выравнивание vec4), так что 16 + 4 + 4 = 24 -> выравниваем до 32
    // // pbr_material->layout.uniform_buffer_size = 32;
    // // pbr_material->default_uniform_data.resize(32, 0);
    //
    // // ---
    //
    // using namespace entt::literals;
    //
    // // 2. Создаем сущность
    // auto entity = registry.create();
    // registry.emplace<engine::Transform>(entity);
    // registry.emplace<engine::MeshFilter>(entity, res_manager.Get<engine::graphics::Mesh>(1));
    //
    // // 3. Создаем ИНСТАНС материала для этой конкретной сущности
    // auto* mat_inst = new engine::graphics::MaterialInstance(pbr_material);
    // // mat_inst->SetParam("base_color", glm::vec4(1.0f, 0.5f, 0.2f, 1.0f));
    // // mat_inst->SetParam("roughness", 0.3f);
    // // mat_inst->SetParam("metallic", 0.8f);
    //
    // // Биндим текстуру в слот 0 (как этого ждет твой шейдер)
    // mat_inst->SetTexture(0, res_manager.Get<engine::graphics::Texture>(1)->handle,
    // render_system.GetRenderer().GetCommonSampler());
    //
    // // 4. Вешаем на сущность!
    // registry.emplace<engine::graphics::MeshRenderer>(entity, mat_inst);
}

}  // namespace editor