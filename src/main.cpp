#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

#include <algorithm>
#include <entt/entt.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <vector>

#include "EngineTypes.hpp"
#include "ResourceManager.hpp"
#include "core/camera.hpp"
#include "core/window_manager.hpp"
#include "render/renderer.hpp"

constexpr uint WindowWidth = (uint)(1920 / 1.5f);
constexpr uint WindowHeight = (uint)(1080 / 1.5f);

int main(int, char*[]) {
    WindowManager windowManager;
    if (!windowManager.Initialize(WindowWidth, WindowHeight, "tryengine")) {
        SDL_Log("Failed to initialize WindowManager");
        return -1;
    }

    Renderer renderer;
    renderer.Init(windowManager);

    RenderTarget target(windowManager.GetDevice(), WindowWidth, WindowHeight, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Управление клавиатурой
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;      // Докинг (вкладки)
    // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;    // ТЕ САМЫЕ ОКНА (Multi-viewports)

    ImGui::StyleColorsDark();

    // Настройка бэккендов
    ImGui_ImplSDL3_InitForSDLGPU(windowManager.GetWindow());

    ImGui_ImplSDLGPU3_InitInfo init_info = {};
    init_info.Device = windowManager.GetDevice();
    init_info.ColorTargetFormat =
        SDL_GetGPUSwapchainTextureFormat(windowManager.GetDevice(), windowManager.GetWindow());
    init_info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    init_info.PresentMode = SDL_GPU_PRESENTMODE_VSYNC;
    ImGui_ImplSDLGPU3_Init(&init_info);

    ResourceManager resources(windowManager.GetDevice());
    std::vector<Mesh*> boxMeshes = resources.LoadModel("assets/fantasy_game_inn/scene.gltf");
    std::vector<Mesh*> redBoxM = resources.LoadModel("assets/red_cube/red_cube.gltf");
    std::vector<Mesh*> matilda = resources.LoadModel("assets/matilda/scene.gltf");

    entt::registry reg;

    if (!boxMeshes.empty() && !redBoxM.empty()) {
        auto e1 = reg.create();
        reg.emplace<MeshComponent>(e1, boxMeshes[0]);
        reg.emplace<TransformComponent>(e1, glm::vec3(-31.f, -5.0f, -7), glm::vec3(0, 0, 0), glm::vec3(1));

        for (uint i = 0; i < matilda.size(); ++i) {
            auto e2 = reg.create();
            reg.emplace<MeshComponent>(e2, matilda[i]);
            reg.emplace<TransformComponent>(e2, glm::vec3(2, 0, -5), glm::vec3(0, 0, 0), glm::vec3(0.01f));
        }

        auto e3 = reg.create();
        reg.emplace<MeshComponent>(e3, redBoxM[0]);
        reg.emplace<TransformComponent>(e3, glm::vec3(-2, 1, -6), glm::vec3(0, 0, 0), glm::vec3(1, 1, 1));
    }

    Camera camera;
    camera.pos = glm::vec3(0.0f, 0.0f, -3.0f);

    uint64_t lastTime = SDL_GetTicksNS();
    double fpsTimer = 0.0;
    // int frameCount = 0;

    bool running = true;
    bool show_demo = true;     // Для теста
    bool vsyncEnabled = true;  // По умолчанию включено, как в init_info

    bool isCursorCaptured = true;
    bool fullScreen = true;
    SDL_SetWindowRelativeMouseMode(windowManager.GetWindow(), true);

    while (running) {
        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);

            if (event.type == SDL_EVENT_QUIT) running = false;

            // Переключение режима по ESC
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
                isCursorCaptured = !isCursorCaptured;

                // Включаем или выключаем относительный режим мыши
                SDL_SetWindowRelativeMouseMode(windowManager.GetWindow(), isCursorCaptured);
            }

            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F11) {
                fullScreen = !fullScreen;

                // Включаем или выключаем относительный режим мыши
                windowManager.SetFullscreen(fullScreen);
            }

            // Обработка движения камеры (только если мышь захвачена)
            if (isCursorCaptured && event.type == SDL_EVENT_MOUSE_MOTION) {
                camera.yaw += event.motion.xrel * camera.sensitivity;
                camera.pitch -= event.motion.yrel * camera.sensitivity;
                camera.pitch = std::clamp(camera.pitch, -89.0f, 89.0f);
            }
        }

        uint64_t currentTime = SDL_GetTicksNS();
        double deltaTime = static_cast<double>(currentTime - lastTime) / 1000000000.0;
        lastTime = currentTime;
        // double totalTime = static_cast<double>(currentTime) / 1000000000.0;

        fpsTimer += deltaTime;
        // frameCount++;

        if (fpsTimer >= 1.0) {
            // SDL_Log("FPS: %d (ms per frame: %.3f)", frameCount, 1000.0 / frameCount);
            fpsTimer -= 1.0;
            // frameCount = 0;
        }

        ImGui_ImplSDLGPU3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        ImGuiViewport* viewport = ImGui::GetMainViewport();

        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::SetNextWindowViewport(viewport->ID);

        ImGuiWindowFlags host_window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                             ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                             ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDocking;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        ImGui::Begin("MainDockSpace", nullptr, host_window_flags);
        ImGui::PopStyleVar(3);

        ImGuiID dockspace_id = ImGui::GetID("MainDockSpaceDock");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

        ImGui::End();

        ImGui::Begin("Scene");
        ImVec2 sceneViewportSize = ImGui::GetContentRegionAvail();

        // Проверяем, изменился ли размер и не равен ли он нулю
        if (sceneViewportSize.x > 0 && sceneViewportSize.y > 0) {
            uint32_t newWidth = (uint32_t)sceneViewportSize.x;
            uint32_t newHeight = (uint32_t)sceneViewportSize.y;
            if (newWidth != target.GetWidth() || newHeight != target.GetHeight()) target.Resize(newWidth, newHeight);
            ImGui::Image((ImTextureID)target.GetColor(), sceneViewportSize);
        }
        ImGui::End();
        if (show_demo) ImGui::ShowDemoWindow(&show_demo);

        ImGui::Begin("Engine Control");
        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
        ImGui::Checkbox("Show Demo Window", &show_demo);
        if (ImGui::Checkbox("Enable VSync", &vsyncEnabled)) {
            // Если состояние изменилось, вызываем метод WindowManager
            windowManager.SetVSync(vsyncEnabled);

            // Полезно вывести лог, чтобы видеть в консоли момент переключения
            SDL_Log("VSync changed to: %s", vsyncEnabled ? "ON" : "OFF");
        }
        ImGui::End();

        ImGui::Begin("Scene Hierarchy");

        auto view_entitiess = reg.view<TransformComponent, MeshComponent>();

        int node_id = 0;
        for (auto entity : view_entitiess) {
            auto& transform = view_entitiess.get<TransformComponent>(entity);

            // Создаем уникальное имя для каждого объекта в списке
            char label[64];
            sprintf(label, "Entity %d", node_id++);

            if (ImGui::TreeNode(label)) {
                ImGui::DragFloat3("Position", &transform.position.x, 0.1f);
                ImGui::DragFloat3("Rotation", &transform.rotation.x, 1.0f);
                ImGui::DragFloat3("Scale", &transform.scale.x, 0.05f);
                ImGui::TreePop();
            }
            ImGui::Separator();
        }

        ImGui::End();

        // Завершаем генерацию геометрии ImGui
        ImGui::Render();
        ImDrawData* draw_data = ImGui::GetDrawData();

        UpdateCamera(camera, deltaTime);
        auto cmd = SDL_AcquireGPUCommandBuffer(windowManager.GetDevice());

        SDL_GPUTexture* swapchainTexture = nullptr;
        uint32_t swapW, swapH;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, windowManager.GetWindow(), &swapchainTexture, &swapW, &swapH)) {
            SDL_SubmitGPUCommandBuffer(cmd);
            continue;
        }
        windowManager.UpdateSizeInternal(swapW, swapH);
        ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, cmd);

        if (sceneViewportSize.x > 0 && sceneViewportSize.y > 0) {
            SDL_GPURenderPass* scenePass = renderer.BeginRenderPass(cmd, target, {0.69f, 0.77f, 0.87f, 1.0f});
            SDL_BindGPUGraphicsPipeline(scenePass, renderer.GetDefaultPipeline());

            float aspect = (float)target.GetWidth() / (float)target.GetHeight();
            glm::mat4 proj = glm::perspective(glm::radians(70.0f), aspect, 0.1f, 100.0f);
            glm::mat4 view = glm::lookAt(camera.pos, camera.pos + camera.front, camera.up);

            LightUniforms lightData{};
            lightData.lightPos = glm::vec4(2.0f, 30.0f, 3.0f, 1.0f);   // Лампочка справа-сверху
            lightData.lightColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);  // Белый свет
            lightData.viewPos = glm::vec4(camera.pos, 1.0f);           // Позиция камеры (на будущее)

            // Отправляем данные во фрагментный шейдер (Slot 0 соответствует binding 0 в шейдере)
            SDL_PushGPUFragmentUniformData(cmd, 0, &lightData, sizeof(LightUniforms));

            auto view_entities = reg.view<TransformComponent, MeshComponent>();

            for (auto entity : view_entities) {
                auto& transform = view_entities.get<TransformComponent>(entity);
                auto& meshComp = view_entities.get<MeshComponent>(entity);

                if (meshComp.mesh == nullptr) {
                    continue;
                }
                glm::mat4 model = transform.GetModelMatrix();
                // model = glm::rotate(model, (float)totalTime, glm::vec3(0.0f, 1.0f, 0.0f));
                glm::mat4 normalMatrix = glm::inverseTranspose(model);
                UniformBufferObject ubo{};
                ubo.proj = proj;
                ubo.view = view;
                ubo.model = model;
                ubo.normalMatrix = normalMatrix;
                SDL_PushGPUVertexUniformData(cmd, 0, &ubo, sizeof(UniformBufferObject));

                SDL_GPUTextureSamplerBinding tsb = {meshComp.mesh->texture->handle, renderer.GetCommonSampler()};
                SDL_BindGPUFragmentSamplers(scenePass, 0, &tsb, 1);

                SDL_GPUBufferBinding vb = {meshComp.mesh->vertexBuffer, 0};
                SDL_BindGPUVertexBuffers(scenePass, 0, &vb, 1);

                SDL_GPUBufferBinding ib = {meshComp.mesh->indexBuffer, 0};
                SDL_BindGPUIndexBuffer(scenePass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);

                SDL_DrawGPUIndexedPrimitives(scenePass, meshComp.mesh->numIndices, 1, 0, 0, 0);
            }
            SDL_EndGPURenderPass(scenePass);
        }

        SDL_GPURenderPass* uiPass = renderer.BeginRenderToWindow(cmd, swapchainTexture);
        ImGui_ImplSDLGPU3_RenderDrawData(draw_data, cmd, uiPass);
        SDL_EndGPURenderPass(uiPass);

        SDL_SubmitGPUCommandBuffer(cmd);
    }

    SDL_WaitForGPUIdle(windowManager.GetDevice());
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    resources.Cleanup();  // Удаляет меши и текстуры
    renderer.Cleanup();   // Удаляет пайплайн, семплер, текстуру глубины
    windowManager.Terminate();
    return 0;
}
