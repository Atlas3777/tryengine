#pragma once
#include <glm/fwd.hpp>

// 1. Описываем саму структуру C++ внутри твоего нэймспейса
namespace tryengine::core {
struct SpawnPoint {
    int32_t id;
    float x;
    float y;
};

// Вспомогательная функция создания (фабрика)
inline SpawnPoint make_spawn_point(int32_t id, float x, float y) {
    return SpawnPoint{ id, x, y };
}
}