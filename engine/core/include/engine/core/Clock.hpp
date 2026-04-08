#pragma once
#include <chrono>

namespace tryengine::core {

struct TimeState {
    double deltaTime = 0.0;
    double fpsTimer = 0.0;
    int frameCount = 0;
    int currentFPS = 0;
};

class Clock {
public:
    Clock();

    // Обновляет deltaTime и FPS. Возвращает текущий TimeState.
    const TimeState& Update();

    double GetDeltaTime() const { return state.deltaTime; }
    int GetFPS() const { return state.currentFPS; }

private:
    TimeState state;
    
    // Используем steady_clock для защиты от скачков времени
    std::chrono::steady_clock::time_point lastTime;
};

} // namespace tryengine::core