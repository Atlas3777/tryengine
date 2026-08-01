#pragma once

#include <chrono>

namespace tryengine::core {

class Time {
public:
    Time();

    void NewFrame();

    [[nodiscard]] float DeltaTime() const noexcept { return raw_dt_ * scale_; }
    [[nodiscard]] float UnscaledDeltaTime() const noexcept { return raw_dt_; }
    [[nodiscard]] float TotalTime() const noexcept { return total_time_; }
    [[nodiscard]] uint32_t FrameCount() const noexcept { return frame_count_; }

    void SetScale(float scale) noexcept { scale_ = scale; }
    [[nodiscard]] float GetScale() const noexcept { return scale_; }

private:
    float raw_dt_ = 0.0f;
    float total_time_ = 0.0f;
    float scale_ = 1.0f;
    uint32_t frame_count_ = 0;

    std::chrono::steady_clock::time_point last_time_;
};

}  // namespace tryengine::core