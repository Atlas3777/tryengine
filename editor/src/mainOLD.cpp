// #include <entt/entt.hpp>
//
// #include "editor/Components.hpp"
// #include "engine/Components.hpp"
// #include "engine/Engine.hpp"
// #include "engine/Renderer.hpp"
// #include "engine/ResourceManager.hpp"
// #include "glm/gtc/matrix_inverse.hpp"
// #include "editor/BaseSystem.hpp"
//
// namespace editor {
// using namespace engine::types;
// using namespace engine::components;
// }  // namespace editor
// // void RegisterReflection() {
// //     entt::meta_factory<Tag>()
// //         .type(entt::type_hash<Tag>::value(), "Tag")
// //         .data<&Tag::tag>("Tag");  // Для полей достаточно просто строки
//
// //     entt::meta_factory<Transform>()
// //         .type(entt::type_hash<Transform>::value(), "Transform")
// //         .data<&Transform::position>("Position")
// //         .data<&Transform::rotation>("Rotation")
// //         .data<&Transform::scale>("Scale");
//
// //     entt::meta_factory<Hierarchy>()
// //         .type(entt::type_hash<Hierarchy>::value(), "Hierarchy")
// //         .data<&Hierarchy::parent>("Parent")
// //         .data<&Hierarchy::depth>("Depth");
//
// //     entt::meta_factory<Mesh>().type(entt::type_hash<Mesh>::value(), "Mesh").data<&Mesh::mesh>("MeshPtr");
// //     entt::meta_factory<AABB>()
// //         .type(entt::type_hash<AABB>::value(), "AABB")
// //         .data<&AABB::worldMax>("Max")
// //         .data<&AABB::worldMin>("Min");
// // }
// int main2(int argc, char* argv[]) {
//     engine::Engine engine;
//     engine.MountHardware();
//
//     engine::Renderer renderer;
//     renderer.Init(engine.GetGraphicsContext().GetDevice());
//
//     engine::ResourceManager resources(engine.GetGraphicsContext().GetDevice());
//     std::vector<engine::Mesh*> taverna = resources.LoadModel("game/assets/fantasy_game_inn/scene.gltf");
//     std::vector<engine::Mesh*> redBoxM = resources.LoadModel("game/assets/red_cube/red_cube.gltf");
//     // std::vector<Mesh*> matilda = resources.LoadModel("assets/matilda/scene.gltf");
//     std::vector<engine::Mesh*> skull = resources.LoadModel("game/assets/skull_downloadable/scene.gltf");
//
//     entt::registry reg;
//
//     using engine::components::AABB;
//     using engine::components::Camera;
//     using engine::components::Hierarchy;
//     using engine::components::MeshRenderer;
//     using engine::components::Tag;
//     using engine::components::Transform;
//
//     auto e1 = reg.create();
//     reg.emplace<Tag>(e1, "Taverna");
//     reg.emplace<MeshRenderer>(e1, taverna[0]);
//     reg.emplace<Transform>(e1, Transform{glm::vec3(0.f), glm::quat(), glm::vec3(1.f)});
//     reg.emplace<Hierarchy>(e1);
//     reg.emplace<AABB>(e1);
//
//     auto e2 = reg.create();
//     reg.emplace<Tag>(e2, "Skull");
//     reg.emplace<MeshRenderer>(e2, skull[0]);
//     reg.emplace<Transform>(e2, Transform{glm::vec3(2.f, 0,0), glm::quat(glm::radians(glm::vec3(0, 0, 0))), glm::vec3(1.f)});
//     reg.emplace<Hierarchy>(e2);
//     reg.emplace<AABB>(e2);
//
//     // for (uint i = 0; i < matilda.size(); ++i) {
//     //     auto e2 = reg.create();
//     //     reg.emplace<Mesh>(e2, matilda[i]);
//     //     reg.emplace<Transform>(e2, glm::vec3(2, 0, -5), glm::vec3(0, 0, 0), glm::vec3(0.01f));
//     // }
//
//     auto e3 = reg.create();
//     reg.emplace<Tag>(e3, "Red Box");
//     reg.emplace<MeshRenderer>(e3, redBoxM[0]);
//     reg.emplace<Transform>(e3, Transform{glm::vec3(1.f), glm::quat(glm::radians(glm::vec3(0, 0, 0))), glm::vec3(1.f)});
//     reg.emplace<Hierarchy>(e3, e2, 1);
//     reg.emplace<AABB>(e3);
//
//     auto mainCamera = reg.create();
//     reg.emplace<Tag>(mainCamera, "EditorCamera");
//     reg.emplace<Transform>(
//         mainCamera, Transform{glm::vec3(0.f, 0.f, -2.f), glm::quat(glm::radians(glm::vec3(0, 0, 0))), glm::vec3(1.f)});
//     reg.emplace<Camera>(mainCamera);
//     reg.emplace<editor::EditorCameraTag>(mainCamera);
//
//     while (engine.isRunning) {
//         engine.ProcessInput();
//         engine.UpdateTime();
//         engine.DispatchCommands();
//
//         editor::UpdateEditorCameraSystem(reg, engine.time.deltaTime, engine.input);
//
//         // Обновляем логику редактора ПЕРЕД системами трансформации и рендером
//         // if (engine.settings.isEditorMode) {
//         //     engine.GetEditorLayer().Update(engine, reg);
//         // }
//
//         // UpdateTransformSystem(reg);
//         // UpdateAABBSystem(reg);
//
//         engine.Render(reg, [&](SDL_GPUCommandBuffer* cmd, engine::RenderTarget* target) {
//             auto camView = reg.view<editor::EditorCameraTag, Transform, Camera>();
//             entt::entity camEnt = camView.front();
//
//             if (camEnt == entt::null) return;
//
//             auto& camTransform = camView.get<Transform>(camEnt);
//             auto& camera = camView.get<Camera>(camEnt);
//
//             float aspect = static_cast<float>(target->GetWidth()) / static_cast<float>(target->GetHeight());
//             glm::mat4 projMat = glm::perspective(glm::radians(camera.fov), aspect, camera.nearPlane, camera.farPlane);
//             glm::mat4 viewMat = camera.viewMatrix;
//             glm::vec3 camPos = camTransform.position;
//
//             SDL_GPURenderPass* scenePass = renderer.BeginRenderPass(cmd, *target, {0.69f, 0.77f, 0.87f, 1.0f});
//
//             engine::LightUniforms lightData{};
//             lightData.lightPos = glm::vec4(2.0f, 30.0f, 3.0f, 1.0f);
//             lightData.lightColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
//             lightData.viewPos = glm::vec4(camPos, 1.0f);
//
//             SDL_PushGPUFragmentUniformData(cmd, 0, &lightData, sizeof(engine::LightUniforms));
//
//             SDL_BindGPUGraphicsPipeline(scenePass, renderer.GetDefaultPipeline());
//
//             for (auto view_entities = reg.view<Transform, MeshRenderer>(); auto entity : view_entities) {
//                 auto& transform = view_entities.get<Transform>(entity);
//                 auto& [mesh] = view_entities.get<MeshRenderer>(entity);
//
//                 if (mesh == nullptr) {
//                     continue;
//                 }
//
//                 struct alignas(16) CombinedUBO {
//                     glm::mat4 view;
//                     glm::mat4 proj;
//                     glm::mat4 model;
//                     glm::mat4 normalMatrix;
//                 } ubo{};
//
//                 ubo.view = viewMat;
//                 ubo.proj = projMat;
//                 ubo.model = transform.worldMatrix;
//                 ubo.normalMatrix = glm::inverseTranspose(ubo.model);
//
//                 SDL_PushGPUVertexUniformData(cmd, 0, &ubo, sizeof(CombinedUBO));
//                 SDL_GPUTextureSamplerBinding tsb = {mesh->texture->handle, renderer.GetCommonSampler()};
//                 SDL_BindGPUFragmentSamplers(scenePass, 0, &tsb, 1);
//
//                 SDL_GPUBufferBinding vb = {mesh->vertexBuffer, 0};
//                 SDL_BindGPUVertexBuffers(scenePass, 0, &vb, 1);
//
//                 SDL_GPUBufferBinding ib = {mesh->indexBuffer, 0};
//                 SDL_BindGPUIndexBuffer(scenePass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
//
//                 SDL_DrawGPUIndexedPrimitives(scenePass, mesh->numIndices, 1, 0, 0, 0);
//             }
//             SDL_EndGPURenderPass(scenePass);
//         });
//     }
//
//     SDL_WaitForGPUIdle(engine.GetGraphicsContext().GetDevice());
//     resources.Cleanup();
//     renderer.Cleanup();
//     return 0;
// }
// // }  // namespace editor
