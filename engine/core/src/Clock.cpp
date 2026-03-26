#include "engine/core/Clock.hpp"

namespace engine::core {

Clock::Clock() {
    lastTime = std::chrono::steady_clock::now();
}

const TimeState& Clock::Update() {
    const auto currentTime = std::chrono::steady_clock::now();

    const std::chrono::duration<double> elapsed = currentTime - lastTime;
    state.deltaTime = elapsed.count();
    lastTime = currentTime;

    state.fpsTimer += state.deltaTime;
    state.frameCount++;

    if (state.fpsTimer >= 1.0) {
        state.currentFPS = state.frameCount;
        state.fpsTimer = 0.0;
        state.frameCount = 0;
    }

    return state;
}

} // namespace engine::core