#pragma once

#include <chrono>
#include <string_view>
#include <unordered_map>
#include <mutex>
#include <algorithm>

namespace tryengine::core {

constexpr size_t PROFILER_HISTORY_SIZE = 600;

struct ProfileMetric {
    std::string_view name;
    uint32_t call_count = 0;
    float total_time_ms = 0.0f;
    float avg_time_ms = 0.0f;
    float max_time_ms = 0.0f;

    // Кольцевой буфер истории вызовов для графика
    float history[PROFILER_HISTORY_SIZE] = {};
    size_t history_offset = 0;
};

class Profiler {
public:
    static Profiler& Instance() {
        static Profiler instance;
        return instance;
    }

    void Record(std::string_view name, float duration_ms) {
        std::lock_guard lock(mutex_);
        auto& sample = current_frame_samples_[name];
        sample.call_count++;
        sample.total_time_ms += duration_ms;
        if (duration_ms > sample.max_time_ms) {
            sample.max_time_ms = duration_ms;
        }
    }

    // Вызывается в конце кадра
    void EndFrame(float frame_delta_time_ms) {
        std::lock_guard lock(mutex_);

        // 1. Записываем общее время кадра
        frame_time_history_[frame_history_offset_] = frame_delta_time_ms;
        frame_history_offset_ = (frame_history_offset_ + 1) % PROFILER_HISTORY_SIZE;

        // 2. Обновляем метрики зон
        for (auto& [name, metric] : metrics_) {
            auto it = current_frame_samples_.find(name);
            if (it != current_frame_samples_.end()) {
                const auto& sample = it->second;
                metric.call_count = sample.call_count;
                metric.total_time_ms = sample.total_time_ms;
                metric.avg_time_ms = sample.total_time_ms / sample.call_count;
                metric.max_time_ms = sample.max_time_ms;

                // Добавляем значение в историю этой функции
                metric.history[metric.history_offset] = sample.total_time_ms;
            } else {
                // Если функция не вызывалась в этом кадре — пишем 0
                metric.call_count = 0;
                metric.total_time_ms = 0.0f;
                metric.avg_time_ms = 0.0f;
                metric.history[metric.history_offset] = 0.0f;
            }
            metric.history_offset = (metric.history_offset + 1) % PROFILER_HISTORY_SIZE;
        }

        // Регистрируем новые метрики, если они появились впервые
        for (auto& [name, sample] : current_frame_samples_) {
            if (metrics_.find(name) == metrics_.end()) {
                ProfileMetric metric;
                metric.name = name;
                metric.call_count = sample.call_count;
                metric.total_time_ms = sample.total_time_ms;
                metric.avg_time_ms = sample.total_time_ms / sample.call_count;
                metric.max_time_ms = sample.max_time_ms;
                metric.history[0] = sample.total_time_ms;
                metric.history_offset = 1;
                metrics_[name] = metric;
            }
        }

        current_frame_samples_.clear();
    }

    const auto& GetMetrics() const { return metrics_; }
    const float* GetFrameTimeHistory() const { return frame_time_history_; }
    size_t GetFrameHistoryOffset() const { return frame_history_offset_; }

private:
    struct Sample {
        uint32_t call_count = 0;
        float total_time_ms = 0.0f;
        float max_time_ms = 0.0f;
    };

    std::mutex mutex_;
    std::unordered_map<std::string_view, Sample> current_frame_samples_;
    std::unordered_map<std::string_view, ProfileMetric> metrics_;

    float frame_time_history_[PROFILER_HISTORY_SIZE] = {};
    size_t frame_history_offset_ = 0;
};

// RAII Scope & macros
class ProfileScope {
public:
    explicit ProfileScope(std::string_view name)
        : name_(name), start_(std::chrono::steady_clock::now()) {}

    ~ProfileScope() {
        auto end = std::chrono::steady_clock::now();
        float duration = std::chrono::duration<float, std::milli>(end - start_).count();
        Profiler::Instance().Record(name_, duration);
    }

private:
    std::string_view name_;
    std::chrono::steady_clock::time_point start_;
};

} // namespace tryengine::core

#define TRY_PROFILE_CONCAT_IMPL(a, b) a##b
#define TRY_PROFILE_CONCAT(a, b) TRY_PROFILE_CONCAT_IMPL(a, b)
#define TRY_PROFILE_SCOPE(name) ::tryengine::core::ProfileScope TRY_PROFILE_CONCAT(_profile_scope_, __LINE__)(name)
#define TRY_PROFILE_FUNCTION() TRY_PROFILE_SCOPE(__FUNCTION__)