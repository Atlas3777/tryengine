#pragma once

#include <EASTL/vector.h>
#include <chrono>
#include <type_traits>

#include "engine/core/Assert.hpp"
#include "engine/core/Clock.hpp"
#include "engine/core/TypeRegistry.hpp"

namespace tryengine::core {

struct RootClock {};
struct GameClock {};
struct GameUIClock {};
struct GameWorldClock {};

class TimeManager {
public:
    TimeManager() {
        last_time_ = std::chrono::steady_clock::now();
        RegisterClock<RootClock, void>();
    }

    template <typename Tag, typename ParentTag = RootClock>
    Clock& RegisterClock(const float scale = 1.0f) {
        const auto type_id = ScopedTypeId<TimeManager, Tag>::Value();

        if (type_id >= tag_to_clock_id_.size())
            tag_to_clock_id_.resize(type_id + 1, Clock::kInvalidId);

        TRY_ASSERT(tag_to_clock_id_[type_id] == Clock::kInvalidId, "Clock already registered!");

        uint32_t parent_clock_id = Clock::kInvalidId;

        if constexpr (!std::is_same_v<ParentTag, void>) {
            const auto parent_type_id = ScopedTypeId<TimeManager, ParentTag>::Value();

            TRY_ASSERT(
                parent_type_id < tag_to_clock_id_.size() && tag_to_clock_id_[parent_type_id] != Clock::kInvalidId,
                "Parent clock must be registered before its child!");

            parent_clock_id = tag_to_clock_id_[parent_type_id];
        }

        const uint32_t clock_id = static_cast<uint32_t>(clocks_.size());
        tag_to_clock_id_[type_id] = clock_id;

        auto& clock = clocks_.emplace_back();
        clock.parent_id_ = parent_clock_id;
        clock.scale_ = scale;

        return clock;
    }

    template <typename Tag>
    [[nodiscard]] Clock& Get() noexcept {
        const auto type_id = ScopedTypeId<TimeManager, Tag>::Value();
        TRY_ASSERT(type_id < tag_to_clock_id_.size() && tag_to_clock_id_[type_id] != Clock::kInvalidId,
                   "Clock is not registered!");
        return clocks_[tag_to_clock_id_[type_id]];
    }

    template <typename Tag>
    [[nodiscard]] const Clock& Get() const noexcept {
        const auto type_id = ScopedTypeId<TimeManager, Tag>::Value();
        TRY_ASSERT(type_id < tag_to_clock_id_.size() && tag_to_clock_id_[type_id] != Clock::kInvalidId,
                   "Clock is not registered!");
        return clocks_[tag_to_clock_id_[type_id]];
    }

    [[nodiscard]] Clock& Root() noexcept { return Get<RootClock>(); }
    [[nodiscard]] const Clock& Root() const noexcept { return Get<RootClock>(); }

    void NewFrame() {
        const auto current_time = std::chrono::steady_clock::now();
        const std::chrono::duration<float> elapsed = current_time - last_time_;
        last_time_ = current_time;

        raw_dt_ = elapsed.count();
        frame_count_++;

        const size_t count = clocks_.size();
        for (size_t i = 0; i < count; ++i) {
            auto& clock = clocks_[i];

            const float parent_dt = (clock.parent_id_ == Clock::kInvalidId) ? raw_dt_ : clocks_[clock.parent_id_].dt_;

            if (clock.is_paused_)
                clock.dt_ = 0.0f;
            else
                clock.dt_ = parent_dt * clock.scale_;

            clock.total_time_ += clock.dt_;
        }
    }

    [[nodiscard]] uint32_t FrameCount() const noexcept { return frame_count_; }

private:
    eastl::vector<Clock> clocks_;  // Плотный массив (топологически отсортирован по факту порядка регистрации)
    eastl::vector<uint32_t> tag_to_clock_id_;  // Разреженный вектор-маппинг ScopedTypeId -> Index в clocks_

    float raw_dt_ = 0.0f;
    uint32_t frame_count_ = 0;
    std::chrono::steady_clock::time_point last_time_;
};

}  // namespace tryengine::core