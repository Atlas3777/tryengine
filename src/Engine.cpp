#include "Engine.hpp"

#include "GraphicsContext.hpp"
#include "RenderTarget.hpp"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"

Engine::Engine(EngineConfig config) { settings.isEditorMode = config.isEditorMode; }

void Engine::MountHardware() {
    graphicsContext = std::make_unique<GraphicsContext>();

    if (!graphicsContext->Initialize(1280, 720, "tryengine")) {
        SDL_Log("Failed to initialize WindowManager");
        return;
    }

    target =
        std::make_unique<RenderTarget>(graphicsContext->GetDevice(), 1280, 720, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);

    if (this->settings.isEditorMode) {
        editor = std::make_unique<EditorLayer>(*graphicsContext);
    }
}

void Engine::UpdateTime() {
    uint64_t currentTime = SDL_GetTicksNS();
    if (time.lastTime == 0) {
        time.lastTime = currentTime;
    }

    time.deltaTime = static_cast<double>(currentTime - time.lastTime) / 1000000000.0;
    time.lastTime = currentTime;

    time.fpsTimer += time.deltaTime;
    time.frameCount++;

    if (time.fpsTimer >= 1) {
        time.currentFPS = static_cast<int>(std::round(time.frameCount / time.fpsTimer));

        SDL_Log("FPS: %d", time.currentFPS);

        time.fpsTimer = 0;
        time.frameCount = 0;
    }
}
void Engine::PushCommand(const EngineCommand& cmd) {
    std::lock_guard<std::mutex> lock(commandMutex);
    commandQueue.push_back(cmd);
}

void Engine::DispatchCommands() {
    for (const auto& cmd : commandQueue) {
        std::visit(
            [this](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                // Внутри DispatchCommands, в обработке CmdSetPresentMode
                if constexpr (std::is_same_v<T, CmdSetPresentMode>) {
                    SDL_GPUDevice* device = graphicsContext->GetDevice();
                    SDL_Window* window = graphicsContext->GetWindow();

                    // 1. Маппинг нашего Enum на типы SDL
                    SDL_GPUPresentMode targetMode;
                    switch (arg.mode) {
                        case PresentMode::Immediate:
                            targetMode = SDL_GPU_PRESENTMODE_IMMEDIATE;
                            break;
                        case PresentMode::Mailbox:
                            targetMode = SDL_GPU_PRESENTMODE_MAILBOX;
                            break;
                        case PresentMode::VSync:
                            targetMode = SDL_GPU_PRESENTMODE_VSYNC;
                            break;
                    }

                    if (!SDL_WindowSupportsGPUPresentMode(device, window, targetMode)) {
                        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Mode not supported, falling back...");
                        if (targetMode == SDL_GPU_PRESENTMODE_MAILBOX) targetMode = SDL_GPU_PRESENTMODE_IMMEDIATE;
                        if (!SDL_WindowSupportsGPUPresentMode(device, window, targetMode))
                            targetMode = SDL_GPU_PRESENTMODE_VSYNC;
                    }

                    // Применяем
                    if (SDL_SetGPUSwapchainParameters(device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, targetMode)) {
                        // ВАЖНО: Обновляем настройки двигателя реальным значением
                        PresentMode actualMode;
                        switch (targetMode) {
                            case SDL_GPU_PRESENTMODE_IMMEDIATE:
                                actualMode = PresentMode::Immediate;
                                break;
                            case SDL_GPU_PRESENTMODE_MAILBOX:
                                actualMode = PresentMode::Mailbox;
                                break;
                            default:
                                actualMode = PresentMode::VSync;
                                break;
                        }

                        settings.presentMode = actualMode;
                        SDL_Log("Present Mode applied: %d", (int)targetMode);
                    }
                } else if constexpr (std::is_same_v<T, CmdSetFullscreen>) {
                    settings.fullScreenEnable = arg.enable;
                    SDL_SetWindowFullscreen(graphicsContext->GetWindow(), arg.enable);
                    SDL_Log("Fullscreen state changed to: %d", arg.enable);
                    const SDL_DisplayMode* mode =
                        SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(GetGraphicsContext().GetWindow()));
                    SDL_Log("Current Display Rate: %f", mode->refresh_rate);
                } else if constexpr (std::is_same_v<T, CmdSetCursorCapture>) {
                    input.isCursorCaptured = arg.enable;
                    SDL_SetWindowRelativeMouseMode(graphicsContext->GetWindow(), arg.enable);
                    SDL_Log("Cursor capture set to: %d", arg.enable);
                } else if constexpr (std::is_same_v<T, CmdQuit>) {
                    isRunning = false;
                }
            },
            cmd);
    }

    commandQueue.clear();
}

void Engine::Render(entt::registry& reg, const RenderCallback& userRenderFunc) {
    bool isEdit = settings.isEditorMode;

    auto cmd = SDL_AcquireGPUCommandBuffer(graphicsContext->GetDevice());

    SDL_GPUTexture* swapchainTexture = nullptr;
    uint32_t swapW, swapH;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, graphicsContext->GetWindow(), &swapchainTexture, &swapW, &swapH)) {
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }

    if (isEdit) {
        editor->RecordRenderGUICommands(*target, reg, *this);
    } else {
        if (target->GetWidth() != swapW || target->GetHeight() != swapH) {
            target->Resize(swapW, swapH);
        }
    }

    if (target->GetWidth() > 0 && target->GetHeight() > 0) {
        userRenderFunc(cmd, target.get());
    }

    if (isEdit) {
        ImDrawData* draw_data = ImGui::GetDrawData();
        ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, cmd);

        SDL_GPUColorTargetInfo colorInfo{};
        colorInfo.texture = swapchainTexture;
        colorInfo.clear_color = {0, 0, 0, 1};
        colorInfo.load_op = SDL_GPU_LOADOP_CLEAR;
        colorInfo.store_op = SDL_GPU_STOREOP_STORE;

        auto guiPass = SDL_BeginGPURenderPass(cmd, &colorInfo, 1, nullptr);

        ImGui_ImplSDLGPU3_RenderDrawData(draw_data, cmd, guiPass);
        SDL_EndGPURenderPass(guiPass);
    } else {
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

void Engine::ProcessInput() {
    memset(input.keyPressed, 0, sizeof(input.keyPressed));
    input.mouseDeltaX = 0.0f;
    input.mouseDeltaY = 0.0f;

    input.keyboardState = (const bool*)SDL_GetKeyboardState(NULL);

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (settings.isEditorMode) {
            ImGui_ImplSDL3_ProcessEvent(&event);
        }

        if (event.type == SDL_EVENT_QUIT) {
            PushCommand(CmdQuit{});
        }

        if (event.type == SDL_EVENT_MOUSE_MOTION) {
            input.mouseDeltaX += event.motion.xrel;
            input.mouseDeltaY += event.motion.yrel;
            input.mouseX = event.motion.x;
            input.mouseY = event.motion.y;
        }

        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            if (event.button.button < static_cast<uint8_t>(MouseButton::Count)) {
                input.isMouseDown[event.button.button] = true;
            }
        }
        if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            if (event.button.button < static_cast<uint8_t>(MouseButton::Count)) {
                input.isMouseDown[event.button.button] = false;
            }
        }

        if (event.type == SDL_EVENT_KEY_DOWN) {
            if (!event.key.repeat) {
                input.keyPressed[event.key.scancode] = true;
            }

            if (event.key.key == SDLK_F11) {
                // Переключаем текущее состояние и кидаем команду
                PushCommand(CmdSetFullscreen{!settings.fullScreenEnable});
            }
        }
    }
}
