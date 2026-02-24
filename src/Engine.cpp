#include "Engine.hpp"

#include <functional>

#include "RenderTarget.hpp"
#include "imgui_impl_sdl3.h"

void Engine::MountHardware() {
    // Создаем WindowManager
    graphicsContext = std::make_unique<GraphicsContext>();

    if (!graphicsContext->Initialize(1280, 720, "tryengine")) {
        SDL_Log("Failed to initialize WindowManager");
        return;
    }

    target =
        std::make_unique<RenderTarget>(graphicsContext->GetDevice(), 1280, 720, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);

    context = std::make_unique<EngineContext>();
    if (context->isEditorModeEnable) {
        editor = std::make_unique<EditorLayer>(*graphicsContext);
    }
}

void Engine::ProcessInput() {
    // 1. Сброс накопленных за прошлый кадр данных
    memset(context->keyPressed, 0, sizeof(context->keyPressed));
    context->mouseDeltaX = 0.0f;
    context->mouseDeltaY = 0.0f;

    // 2. Клавиатура
    context->keyboardState = (const bool*)SDL_GetKeyboardState(NULL);

    // 3. Состояние кнопок и координат мыши (Абсолютное)
    // SDL_GetMouseState дает текущие X, Y и маску нажатых кнопок
    context->mouseButtons = SDL_GetMouseState(&context->mouseX, &context->mouseY);

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);

        if (event.type == SDL_EVENT_QUIT) context->running = false;

        // Движение мыши (Относительное - дельта)
        if (event.type == SDL_EVENT_MOUSE_MOTION) {
            // Мы суммируем дельту, так как за один кадр может прийти несколько ивентов движения
            context->mouseDeltaX += event.motion.xrel;
            context->mouseDeltaY += event.motion.yrel;
        }

        if (event.type == SDL_EVENT_KEY_DOWN) {
            if (!event.key.repeat) {
                context->keyPressed[event.key.scancode] = true;
            }

            if (event.key.key == SDLK_ESCAPE) {
                context->isCursorCaptured = !context->isCursorCaptured;
                SDL_SetWindowRelativeMouseMode(graphicsContext->GetWindow(), context->isCursorCaptured);
            }
        }
    }
}

void Engine::Render(entt::registry& reg, RenderCallback userRenderFunc) {
    if (context->isEditorModeEnable) {
        // Передаем reg сюда
        editor->RecordRenderGUICommands(*target, reg, *context);
    }
    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();

    auto cmd = SDL_AcquireGPUCommandBuffer(graphicsContext->GetDevice());

    SDL_GPUTexture* swapchainTexture = nullptr;
    uint32_t swapW, swapH;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, graphicsContext->GetWindow(), &swapchainTexture, &swapW, &swapH)) {
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }

    ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, cmd);
    // --- ЛОГИКА РАЗМЕРА ---
    if (context->isEditorModeEnable) {
        // 1. В РЕЖИМЕ РЕДАКТОРА
        // Мы НЕ меняем размер target здесь.
        // Это уже делает EditorLayer внутри ImGui::Begin("Scene"),
        // когда узнает размер своего окна через ImGui::GetContentRegionAvail().
    } else {
        // 2. В РЕЖИМЕ ИГРЫ
        // Target должен строго соответствовать размеру Swapchain
        if (target->GetWidth() != swapW || target->GetHeight() != swapH) {
            target->Resize(swapW, swapH);
        }
    }

    // --- РЕНДЕРИНГ МИРА ---
    // Теперь мы точно знаем, что target нужного размера (либо под ImGui, либо под окно)
    if (target->GetWidth() > 0 && target->GetHeight() > 0) {
        userRenderFunc(cmd, target.get());
    }

    // 5. ФИНАЛЬНЫЙ ВЫВОД
    if (context->isEditorModeEnable) {
        // Вывод через ImGui
        // ImGui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(), cmd);

        SDL_GPUColorTargetInfo colorInfo{};
        colorInfo.texture = swapchainTexture;
        colorInfo.clear_color = {0, 0, 0, 1};
        colorInfo.load_op = SDL_GPU_LOADOP_CLEAR;
        colorInfo.store_op = SDL_GPU_STOREOP_STORE;
        auto guiPass = SDL_BeginGPURenderPass(cmd, &colorInfo, 1, nullptr);

        ImGui_ImplSDLGPU3_RenderDrawData(draw_data, cmd, guiPass);
        SDL_EndGPURenderPass(guiPass);
    } else {
        // РЕЖИМ ИГРЫ: Просто копируем результат target в swapchain
        // В SDL_GPU это делается через Blit
        SDL_GPUBlitInfo blitInfo{};
        blitInfo.source.texture = target->GetColor();
        blitInfo.source.w = target->GetWidth();
        blitInfo.source.h = target->GetHeight();
        blitInfo.destination.texture = swapchainTexture;
        blitInfo.destination.w = swapW;
        blitInfo.destination.h = swapH;
        blitInfo.filter = SDL_GPU_FILTER_LINEAR;

        SDL_BlitGPUTexture(cmd, &blitInfo);
    }

    SDL_SubmitGPUCommandBuffer(cmd);
}
