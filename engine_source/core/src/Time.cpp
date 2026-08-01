#include "engine/core/Time.hpp"

namespace tryengine::core {

Time::Time() {
    last_time_ = std::chrono::steady_clock::now();
}

void Time::NewFrame() {
    const auto current_time = std::chrono::steady_clock::now();
    const std::chrono::duration<float> elapsed = current_time - last_time_;

    raw_dt_ = elapsed.count();
    last_time_ = current_time;

    total_time_ += raw_dt_;
    frame_count_++;
}

}  // namespace tryengine::core