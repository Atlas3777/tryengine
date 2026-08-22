#pragma once

#include <chrono>
#include <cstdint>

namespace tryengine::core {

class Time {
public:
    Time();

    void NewFrame();

    [[nodiscard]] float ScaledDeltaTime() const noexcept { return raw_dt_ * scale_; }
    [[nodiscard]] float UnscaledDeltaTime() const noexcept { return raw_dt_; }

    [[nodiscard]] float ScaledTotalTime() const noexcept { return scaled_total_time_; }
    [[nodiscard]] float UnscaledTotalTime() const noexcept { return unscaled_total_time_; }
    [[nodiscard]] uint32_t FrameCount() const noexcept { return frame_count_; }

    void SetScale(float scale) noexcept { scale_ = scale; }
    [[nodiscard]] float GetScale() const noexcept { return scale_; }

private:
    float raw_dt_ = 0.0f;
    float scaled_total_time_ = 0.0f;
    float unscaled_total_time_ = 0.0f;
    float scale_ = 1.0f;
    uint32_t frame_count_ = 0;

    std::chrono::steady_clock::time_point last_time_;
};

}  // namespace tryengine::core