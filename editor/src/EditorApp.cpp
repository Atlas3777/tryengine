#include "editor/EditorApp.hpp"

#include <daScript/ast/ast.h>
#include <daScript/daScriptModule.h>
#include <daScript/misc/string_writer.h>
#include <daScript/simulate/fs_file_info.h>
#include <imgui_impl_sdl3.h>

#include "daScript/misc/sysos.h"
#include "editor/AppBootstrap.hpp"
#include "editor/Editor.hpp"
#include "editor/InputMapper.hpp"
#include "editor/gui/EditorGUI.hpp"
#include "engine/core/AssetDatabase.hpp"
#include "engine/core/BaseSystem.hpp"
#include "engine/core/Clock.hpp"
#include "engine/core/Engine.hpp"
#include "engine/core/ResourceManager.hpp"
#include "engine/core/SceneManager.hpp"

void InitializeDaScriptModules() {
    NEED_ALL_DEFAULT_MODULES;
    //NEED_MODULE(Module_Engine);  // Теперь макрос корректно сошлется на глобальную функцию
}

namespace tryeditor {

void EditorApp::Init() {
    AppBootstrap::CheckBaseProjectData();

    engine_ = std::make_unique<tryengine::core::Engine>();
    engine_->SetInputSource(&(this->input_state_));

    graphics_context_ = std::make_unique<tryengine::graphics::GraphicsContext>();
    if (!graphics_context_->Initialize(1280, 720, "tryengine")) {
        SDL_Log("Failed to initialize WindowManager");
        return;
    }

    render_system_ = std::make_unique<tryengine::graphics::RenderSystem>(graphics_context_->GetDevice());
    editor_ = std::make_unique<Editor>(*engine_, *graphics_context_);

    editor_->Init();

    editor_->running = true;
    editor_->play_mode = false;


    das::setDasRoot("engine_content");

    InitializeDaScriptModules();
    das::Module::Initialize();

    das::TextPrinter tout;
    das::ModuleGroup dummyLibGroup;

    auto fAccess = das::make_smart<das::FsFileAccess>();

    // Компилируем точку входа
    auto program = das::compileDaScript("game/assets/scripts/game.das", fAccess, tout, dummyLibGroup);
    if (!program->failed()) {
        das_ctx = new das::Context(program->getContextStackSize());
        if (program->simulate(*das_ctx, tout)) {
            // Находим функции
            auto fn_init = das_ctx->findFunction("start");
            fn_game_update = das_ctx->findFunction("update");

            // Вызываем Init
            if (fn_init) {
                das_ctx->evalWithCatch(fn_init, nullptr);
            }
        }
    }
    else {
        std::cout << "Failed to initialize" << std::endl;
    }
}

void EditorApp::Run() {
    while (editor_->running) {
        UpdateInput();
        const auto time_state = engine_->GetClock().Update();

        editor_->GetEditorGUI().UpdatePanels(*engine_);

        tryengine::core::UpdateTransformSystem(engine_->GetSceneManager().GetActiveScene().GetRegistry());
        tryengine::core::UpdateCameraMatrices(engine_->GetSceneManager().GetActiveScene().GetRegistry());

        if (editor_->play_mode && das_ctx && fn_game_update) {
            float dt = static_cast<float>(time_state.delta_time);

            das::das_invoke_function<void>::invoke(das_ctx, nullptr, fn_game_update, dt);
        }

        editor_->GetEditorGUI().RecordPanelsGpuCommands(*engine_, editor_->play_mode);

        const auto cmd = SDL_AcquireGPUCommandBuffer(graphics_context_->GetDevice());

        SDL_GPUTexture* swapchainTexture = nullptr;
        uint32_t w, h;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, graphics_context_->GetWindow(), &swapchainTexture, &w, &h)) {
            SDL_SubmitGPUCommandBuffer(cmd);
            continue;
        }
        editor_->GetEditorGUI().RenderToPanel(cmd, *render_system_, *engine_);

        editor_->GetEditorGUI().RenderPanelsToSwapchain(swapchainTexture, cmd);
    }
}

void EditorApp::UpdateInput() {
    input_state_.ResetFrame();

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);

        if (event.type == SDL_EVENT_QUIT) {
            editor_->running = false;
            continue;
        }

        InputMapper::ProcessEvent(event, input_state_);
    }
}

void EditorApp::Shutdown() {
    std::cout << "EditorApp shutting down..." << std::endl;

    // Освобождаем контекст, если он был выделен
    if (das_ctx) {
        delete das_ctx;
        das_ctx = nullptr;
    }

    // Выгружаем модули daScript
    das::Module::Shutdown();
}

}  // namespace tryeditor
