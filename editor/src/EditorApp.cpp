#include "editor/EditorApp.hpp"

#include <imgui.h>
#include <ImGuizmo.h>

#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

#include "editor/AppBootstrap.hpp"
#include "editor/Editor.hpp"
#include "editor/InputMapper.hpp"
#include "editor/gui/EditorGUI.hpp"
#include "engine/core/BaseSystem.hpp"
#include "engine/core/Clock.hpp"
#include "engine/core/ComponentRegistry.hpp"
#include "engine/core/Engine.hpp"
#include "engine/core/InputService.hpp"
#include "engine/core/ResourceManager.hpp"
#include "engine/core/SceneManager.hpp"
#include "engine/core/ScriptSystem.hpp"
#include "engine/core/SpawnPoint.hpp"

// void RegisterEditorScriptBindings();

namespace tryeditor {

void EditorApp::Init() {
    AppBootstrap::CheckBaseProjectData();
    // RegisterEditorScriptBindings();

    engine_ = std::make_unique<tryengine::core::Engine>();

    auto& resource_manager_ = engine_->RegisterSystem<tryengine::core::ResourceManager>();
    auto& component_registry_ = engine_->RegisterSystem<tryengine::core::ComponentRegistry>();
    engine_->RegisterSystem<tryengine::core::Clock>();
    engine_->RegisterSystem<tryengine::core::SceneManager>(component_registry_, resource_manager_);
    engine_->RegisterSystem<tryengine::core::ScriptSystem>();
    engine_->RegisterSystem<tryengine::core::InputService>(this->input_state_);

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

    auto& script_system = engine_->Get<tryengine::core::ScriptSystem>();
    if (script_system.LoadMainScript("./editor/daslang/EntryPoint.das")) {
        script_system.InvokeStart();
    } else {
        std::cerr << "Failed to compile entry_point.das \n";
    }
    std::vector<tryengine::core::SpawnPoint> vector;
    script_system.InvokeFunction("GetLight", &vector);

    std::cout << vector.size() << "\n";

    for (auto& spawnPoint : vector) {
        std::cout << spawnPoint.x << " " << spawnPoint.y << "\n";
    }
}

void EditorApp::Run() {
    while (editor_->running) {
        UpdateInput();
        const auto time_state = engine_->Get<tryengine::core::Clock>().Update();
        float dt = static_cast<float>(time_state.delta_time);

        editor_->GetEditorGUI().UpdatePanels(*engine_);

        tryengine::core::UpdateTransformSystem(
            engine_->Get<tryengine::core::SceneManager>().GetActiveScene().GetRegistry());
        tryengine::core::UpdateCameraMatrices(
            engine_->Get<tryengine::core::SceneManager>().GetActiveScene().GetRegistry());

        engine_->Get<tryengine::core::ScriptSystem>().CheckForReload(dt);

        if (editor_->play_mode) {
            engine_->Get<tryengine::core::ScriptSystem>().InvokeUpdate(dt);
        }

        ImGui_ImplSDLGPU3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();

        engine_->Get<tryengine::core::ScriptSystem>().InvokeFunction("ren");



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
}

}  // namespace tryeditor
