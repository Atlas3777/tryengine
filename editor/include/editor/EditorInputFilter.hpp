#pragma once

#include <SDL3/SDL_events.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include "engine/platform/ISDLEventFilter.hpp"

namespace tryeditor {

class EditorSDLEventFilter : public tryengine::platform::ISDLEventFilter {
public:
    bool OnSDLEvent(const SDL_Event& event) override {
        ImGui_ImplSDL3_ProcessEvent(&event);

        const ImGuiIO& io = ImGui::GetIO();

        // 1. Клавиатура: забираем только когда открыто текстовое поле
        if ((event.type >= SDL_EVENT_KEY_DOWN && event.type <= SDL_EVENT_KEYMAP_CHANGED) && io.WantTextInput) {
            return true;
        }

        // Клик и движение мыши пропускаем ВЕСЬ в InputState!
        return false;
    }
};

}  // namespace tryeditor