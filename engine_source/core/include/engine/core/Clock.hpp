#pragma once

#include <cstdint>
#include <limits>

namespace tryengine::core {

class TimeManager;

class Clock {
public:
    static constexpr uint32_t kInvalidId = std::numeric_limits<uint32_t>::max();

    Clock() = default;

    void SetScale(float scale) noexcept { scale_ = scale; }
    [[nodiscard]] float GetScale() const noexcept { return scale_; }

    void SetPaused(bool paused) noexcept { is_paused_ = paused; }
    [[nodiscard]] bool IsPaused() const noexcept { return is_paused_; }

    [[nodiscard]] float DeltaTime() const noexcept { return dt_; }
    [[nodiscard]] double TotalTime() const noexcept { return total_time_; }

private:
    friend class TimeManager;

    uint32_t parent_id_ = kInvalidId; // Индекс родителя в ПЛОТНОМ массиве clocks_
    float scale_ = 1.0f;
    float dt_ = 0.0f;
    double total_time_ = 0.0;
    bool is_paused_ = false;
};

}  // namespace tryengine::core