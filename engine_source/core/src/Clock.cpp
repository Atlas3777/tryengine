#include "engine/core/Clock.hpp"

namespace tryengine::core {

Clock::Clock() {
    lastTime = std::chrono::steady_clock::now();
}

const TimeState& Clock::Update() {
    const auto currentTime = std::chrono::steady_clock::now();

    const std::chrono::duration<double> elapsed = currentTime - lastTime;
    state.delta_time = elapsed.count();
    lastTime = currentTime;

    state.fps_timer += state.delta_time;
    state.frame_count++;

    if (state.fps_timer >= 1.0) {
        state.current_fps = state.frame_count;
        state.fps_timer = 0.0;
        state.frame_count = 0;
    }

    return state;
}

}  // namespace tryengine::core
