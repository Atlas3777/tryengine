#pragma once

#include <entt/entt.hpp>

#include "imgui.h"

// Предварительные объявления (Forward declarations) ускоряют компиляцию
class GraphicsContext;
class RenderTarget;
class Engine;

class EditorLayer {
   public:
    EditorLayer(GraphicsContext& context);
    ~EditorLayer();

    // Запрещаем копирование, так как класс владеет ресурсами ImGui
    EditorLayer(const EditorLayer&) = delete;
    EditorLayer& operator=(const EditorLayer&) = delete;

    void RecordRenderGUICommands(RenderTarget& renderTarget, entt::registry& reg, Engine& engine);
};
