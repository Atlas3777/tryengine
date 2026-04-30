#pragma once

namespace tryengine::core {
class Engine;

typedef void (*Game_OnInitFn)(Engine&);
typedef void (*Game_OnUpdateFn)(Engine&, float);
typedef void (*Game_OnShutdownFn)(Engine&);

struct GameLibrary {
    void* handle = nullptr;
    tryengine::core::Game_OnInitFn onInit = nullptr;
    tryengine::core::Game_OnUpdateFn onUpdate = nullptr;
    tryengine::core::Game_OnShutdownFn onShutdown = nullptr;

    bool IsValid() const {
        return handle && onInit && onUpdate && onShutdown;
    }

    void Clear() {
        handle = nullptr;
        onInit = nullptr;
        onUpdate = nullptr;
        onShutdown = nullptr;
    }
};

}

// Экспортные функции (всегда глобальные, без неймспейса)
extern "C" {
    void Game_OnInit(tryengine::core::Engine& engine);
    void Game_OnUpdate(tryengine::core::Engine& engine, float deltaTime);
    void Game_OnShutdown(tryengine::core::Engine& engine);
}