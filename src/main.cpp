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

constexpr uint WindowWidth = 1280;
constexpr uint WindowHeight = 720;

int main(int, char*[]) {
    WindowManager windowManager;
    if (!windowManager.Initialize(WindowWidth, WindowHeight, "tryengine")) {
        SDL_Log("Failed to initialize WindowManager");
        return -1;
    }

    Renderer renderer;
    renderer.Init(windowManager);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Управление клавиатурой
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;      // Докинг (вкладки)
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;    // ТЕ САМЫЕ ОКНА (Multi-viewports)

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
        // Создаем первую сущность (Box 1)
        auto e1 = reg.create();
        reg.emplace<MeshComponent>(e1, boxMeshes[0]);
        reg.emplace<TransformComponent>(e1, glm::vec3(0, 0, 0), glm::vec3(0, 0, 0), glm::vec3(1));

        // // Создаем вторую сущность (Box 2)
        for (uint i = 0; i < matilda.size(); ++i) {
            auto e2 = reg.create();
            reg.emplace<MeshComponent>(e2, matilda[i]);
            reg.emplace<TransformComponent>(e2, glm::vec3(2, 0, -5), glm::vec3(0, 0, 0), glm::vec3(0.01f));
        }

        // Создаем третью сущность (Red Box)
        auto e3 = reg.create();
        reg.emplace<MeshComponent>(e3, redBoxM[0]);
        reg.emplace<TransformComponent>(e3, glm::vec3(-2, 1, -6), glm::vec3(0, 0, 0), glm::vec3(1, 1, 1));
    }

    Camera camera;
    camera.pos = glm::vec3(0.0f, 0.0f, -3.0f);

    uint64_t lastTime = SDL_GetTicksNS();
    double fpsTimer = 0.0;
    int frameCount = 0;

    bool running = true;
    bool show_demo = true;  // Для теста

    bool isCursorCaptured = true;
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
        frameCount++;

        if (fpsTimer >= 1.0) {
            // SDL_Log("FPS: %d (ms per frame: %.3f)", frameCount, 1000.0 / frameCount);
            fpsTimer -= 1.0;
            frameCount = 0;
        }

        ImGui_ImplSDLGPU3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::DockSpaceOverViewport(viewport->ID);  // Передаем ID вьюпорта, а не сам указатель

        // Рисуем тестовое окно ImGui
        if (show_demo) ImGui::ShowDemoWindow(&show_demo);

        ImGui::Begin("Engine Control");
        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
        ImGui::Checkbox("Show Demo Window", &show_demo);
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
                // Слайдеры для позиции
                ImGui::DragFloat3("Position", &transform.position.x, 0.1f);

                // Слайдеры для поворота (в градусах)
                ImGui::DragFloat3("Rotation", &transform.rotation.x, 1.0f);

                // Слайдер для масштаба
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

        auto ctx = renderer.BeginFrame();

        ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, ctx.cmd);
        ctx.pass = SDL_BeginGPURenderPass(ctx.cmd, &ctx.colorTargetInfo, 1, &ctx.depthTargetInfo);

        // Биндим пайплайн (один раз, если он общий для всех кубов)
        SDL_BindGPUGraphicsPipeline(ctx.pass, renderer.GetDefaultPipeline());

        // Матрицы камеры (View/Proj общие для всего кадра)
        glm::mat4 view = glm::lookAt(camera.pos, camera.pos + camera.front, camera.up);
        glm::mat4 proj = glm::perspective(glm::radians(70.0f), (float)WindowWidth / WindowHeight, 0.05f, 100.0f);

        LightUniforms lightData{};
        lightData.lightPos = glm::vec4(2.0f, 30.0f, 3.0f, 1.0f);   // Лампочка справа-сверху
        lightData.lightColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);  // Белый свет
        lightData.viewPos = glm::vec4(camera.pos, 1.0f);           // Позиция камеры (на будущее)

        // Отправляем данные во фрагментный шейдер (Slot 0 соответствует binding 0 в шейдере)
        SDL_PushGPUFragmentUniformData(ctx.cmd, 0, &lightData, sizeof(LightUniforms));

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
            SDL_PushGPUVertexUniformData(ctx.cmd, 0, &ubo, sizeof(UniformBufferObject));

            // 2. Текстуры (лучше вынести загрузку из цикла рендера!)
            // Texture* debugTex = resources.LoadTexture("assets/test.png");
            SDL_GPUTextureSamplerBinding tsb = {meshComp.mesh->texture->handle, renderer.GetCommonSampler()};
            // SDL_GPUTextureSamplerBinding tsb = {debugTex->handle, renderer.GetCommonSampler()};
            SDL_BindGPUFragmentSamplers(ctx.pass, 0, &tsb, 1);

            // 3. Drawing
            SDL_GPUBufferBinding vb = {meshComp.mesh->vertexBuffer, 0};
            SDL_BindGPUVertexBuffers(ctx.pass, 0, &vb, 1);

            SDL_GPUBufferBinding ib = {meshComp.mesh->indexBuffer, 0};
            SDL_BindGPUIndexBuffer(ctx.pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);

            SDL_DrawGPUIndexedPrimitives(ctx.pass, meshComp.mesh->numIndices, 1, 0, 0, 0);
        }
        ImGui_ImplSDLGPU3_RenderDrawData(draw_data, ctx.cmd, ctx.pass);
        renderer.EndFrame(ctx);
        // --- 5. ОБНОВЛЕНИЕ ВНЕШНИХ ОКОН (Viewports) ---
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
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
