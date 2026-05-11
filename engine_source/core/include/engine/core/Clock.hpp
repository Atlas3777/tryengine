#pragma once
#include <chrono>

namespace tryengine::core {

struct TimeState {
    double delta_time = 0.0;
    double fps_timer = 0.0;
    int frame_count = 0;
    int current_fps = 0;
};

class Clock {
public:
    Clock();

    // Обновляет deltaTime и FPS. Возвращает текущий TimeState.
    const TimeState& Update();

    double GetDeltaTime() const { return state.delta_time; }
    int GetFPS() const { return state.current_fps; }

private:
    TimeState state;

    // Используем steady_clock для защиты от скачков времени
    std::chrono::steady_clock::time_point lastTime;
};

}  // namespace tryengine::core
