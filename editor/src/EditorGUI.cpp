// #include "editor/gui/EditorGUI.hpp"
//
// #include <imgui.h>
// #include <imgui_impl_sdl3.h>
// #include <imgui_impl_sdlgpu3.h>
//
// #include "engine/core/Engine.hpp"
// #include "engine/core/Log.hpp"
// #include "engine/core/Profiler.hpp"
// #include "engine/core/ScriptSystem.hpp"
//
// namespace tryeditor {
//
// EditorGUI::EditorGUI(tryengine::core::Engine& engine, const tryengine::graphics::GraphicsContext& context) : engine_(engine) {
//     IMGUI_CHECKVERSION();
//     ImGui::CreateContext();
//     ImGuiIO& io = ImGui::GetIO();
//
//     io.IniFilename = "editor/imgui.ini";
//     io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
//     io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
//     // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
//
//     ImGui::StyleColorsDark();
//
//     // Настройка бэкендов
//     ImGui_ImplSDL3_InitForSDLGPU(context.GetWindow());
//
//     ImGui_ImplSDLGPU3_InitInfo init_info = {};
//     init_info.Device = context.GetDevice();
//     init_info.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(context.GetDevice(), context.GetWindow());
//     init_info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
//     init_info.PresentMode = SDL_GPU_PRESENTMODE_VSYNC;
//     ImGui_ImplSDLGPU3_Init(&init_info);
// }
//
// EditorGUI::~EditorGUI() {
//     ImGui_ImplSDLGPU3_Shutdown();
//     ImGui_ImplSDL3_Shutdown();
//     ImGui::DestroyContext();
// }
//
// static void RenderProfilerPanel() {
//     if (!ImGui::Begin("Performance Profiler")) {
//         ImGui::End();
//         return;
//     }
//
//     auto& profiler = tryengine::core::Profiler::Instance();
//     const float* frame_history = profiler.GetFrameTimeHistory();
//     size_t offset = profiler.GetFrameHistoryOffset();
//
//     // 1. Считаем текущее время кадра и FPS
//     size_t last_idx = (offset + tryengine::core::PROFILER_HISTORY_SIZE - 1) % tryengine::core::PROFILER_HISTORY_SIZE;
//     float current_frame_ms = frame_history[last_idx];
//     float fps = current_frame_ms > 0.0f ? 1000.0f / current_frame_ms : 0.0f;
//
//     // 2. Главный график Frame Time
//     char overlay[64];
//     snprintf(overlay, sizeof(overlay), "Frame: %.2f ms (%.1f FPS)", current_frame_ms, fps);
//
//     ImGui::Text("Frame Time History");
//     ImGui::PlotLines("##FrameTimePlot", frame_history, static_cast<int>(tryengine::core::PROFILER_HISTORY_SIZE),
//                      static_cast<int>(offset), overlay,
//                      0.0f,   // Min ms
//                      33.3f,  // Max ms (шкала до 33мс / ~30 FPS, чтобы наглядно видеть спайки)
//                      ImVec2(ImGui::GetContentRegionAvail().x, 70.0f)  // Ширина во всё окно, высота 70px
//     );
//
//     ImGui::Separator();
//
//     // 3. Таблица метрик с индивидуальными графиками (Sparklines)
//     if (ImGui::BeginTable("ProfilerTable", 5,
//                           ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
//         ImGui::TableSetupColumn("Zone / Function", ImGuiTableColumnFlags_WidthStretch);
//         ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthFixed, 45.0f);
//         ImGui::TableSetupColumn("Total (ms)", ImGuiTableColumnFlags_WidthFixed, 70.0f);
//         ImGui::TableSetupColumn("Avg (ms)", ImGuiTableColumnFlags_WidthFixed, 70.0f);
//         ImGui::TableSetupColumn("History", ImGuiTableColumnFlags_WidthFixed, 120.0f);
//         ImGui::TableHeadersRow();
//
//         for (const auto& [name, metric] : profiler.GetMetrics()) {
//             ImGui::TableNextRow();
//
//             // Имя
//             ImGui::TableSetColumnIndex(0);
//             ImGui::TextUnformatted(metric.name.data());
//
//             // Кол-во вызовов за кадр
//             ImGui::TableSetColumnIndex(1);
//             ImGui::Text("%u", metric.call_count);
//
//             // Суммарное время за кадр
//             ImGui::TableSetColumnIndex(2);
//             ImGui::Text("%.3f", metric.total_time_ms);
//
//             // Среднее время за 1 вызов
//             ImGui::TableSetColumnIndex(3);
//             ImGui::Text("%.3f", metric.avg_time_ms);
//
//             // Мини-график (Sparkline) для этой зоны
//             ImGui::TableSetColumnIndex(4);
//             ImGui::PushID(metric.name.data());
//             ImGui::PlotLines("##sparkline", metric.history, static_cast<int>(tryengine::core::PROFILER_HISTORY_SIZE),
//                              static_cast<int>(metric.history_offset), nullptr, 0.0f,
//                              FLT_MAX,  // Авто-шкала под максимальные значения зоны
//                              ImVec2(120.0f, 18.0f));
//             ImGui::PopID();
//         }
//         ImGui::EndTable();
//     }
//
//     ImGui::End();
// }
//
// void EditorGUI::RecordPanelsGpuCommands(PlayModeState& state) {
//     ImGui_ImplSDLGPU3_NewFrame();
//     ImGui_ImplSDL3_NewFrame();
//     ImGui::NewFrame();
//
//     DrawMainMenu();
//     DrawPlayToolbar(state);
//     DrawDockSpace();
//
//     engine_.Get<tryengine::core::ScriptSystem>().InvokeFunctionSafe("draw_editor");
//
//     RenderProfilerPanel();
//
//     ImGui::Render();
// }
//
// void EditorGUI::DrawDockSpace() {
//     ImGuiViewport* viewport = ImGui::GetMainViewport();
//
//     float toolbar_height = 30.0f;
//
//     // Сдвигаем начало DockSpace на высоту тулбара и уменьшаем его общий размер
//     ImVec2 dock_pos = ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + toolbar_height);
//     ImVec2 dock_size = ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - toolbar_height);
//
//     ImGui::SetNextWindowPos(dock_pos);
//     ImGui::SetNextWindowSize(dock_size);
//     ImGui::SetNextWindowViewport(viewport->ID);
//
//     ImGuiWindowFlags host_window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
//                                          ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
//                                          ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
//                                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDocking;
//
//     ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
//     ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
//     ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
//
//     ImGui::Begin("MainDockSpace", nullptr, host_window_flags);
//     ImGui::PopStyleVar(3);
//
//     ImGuiID dockspace_id = ImGui::GetID("MainDockSpaceDock");
//     ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
//     ImGui::End();
// }
//
// void EditorGUI::DrawMainMenu() {
//     ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImGui::GetStyle().Colors[ImGuiCol_WindowBg]);
//
//     if (ImGui::BeginMainMenuBar()) {
//         if (ImGui::BeginMenu("File")) {
//             if (ImGui::MenuItem("Save Scene")) {
//             }
//             if (ImGui::MenuItem("Save Scene As")) {
//             }
//             ImGui::EndMenu();
//         }
//         if (ImGui::BeginMenu("Edit")) {
//             ImGui::EndMenu();
//         }
//         if (ImGui::BeginMenu("View")) {
//             ImGui::EndMenu();
//         }
//         if (ImGui::BeginMenu("Asset")) {
//             ImGui::EndMenu();
//         }
//         ImGui::EndMainMenuBar();
//     }
//     ImGui::PopStyleColor();
// }
//
// void EditorGUI::DrawPlayToolbar(PlayModeState& state) {
//     ImGuiViewport* viewport = ImGui::GetMainViewport();
//
//     // Задаем жесткую высоту панели
//     float toolbar_height = 30.0f;
//
//     ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y));
//     ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, toolbar_height));
//     ImGui::SetNextWindowViewport(viewport->ID);
//
//     // Флаги, запрещающие изменение размера, перемещение, докинг и скрывающие декорации
//     ImGuiWindowFlags toolbar_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar |
//                                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
//
//     ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
//     ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 8.0f));
//     ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyle().Colors[ImGuiCol_WindowBg]);
//
//     ImGui::Begin("PlayToolbar", nullptr, toolbar_flags);
//
//     // Центрируем кнопки по горизонтали
//     float button_area_width = 120.0f;  // Примерная ширина двух кнопок с отступом
//     ImGui::SetCursorPosX((ImGui::GetWindowWidth() - button_area_width) * 0.5f);
//
//     if (ImGui::Button("Stop/Play", ImVec2(50, 0))) {
//         if (state == PlayModeState::Play) {
//             state = PlayModeState::Edit;
//         } else if (state == PlayModeState::Edit) {
//             state = PlayModeState::Play;
//         }
//     }
//     ImGui::SameLine();
//     if (ImGui::Button("Pause", ImVec2(50, 0))) {
//         LogInfo("Pause click");
//     }
//
//     ImGui::End();
//
//     ImGui::PopStyleColor();
//     ImGui::PopStyleVar(2);
// }
// }  // namespace tryeditor
