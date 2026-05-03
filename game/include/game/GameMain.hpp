#include "RotateSystem.hpp"

namespace game {

class GameMain {
public:
    static void InitializeSystems(tryengine::core::Engine& engine) {
        // Регистрация рефлексии, создание игрока и т.д.
    }

    static void UpdateAllSystems(tryengine::core::Engine& engine, float dt) { Rotate(engine); };

    static void Cleanup(tryengine::core::Engine& engine) {
        // Очистка ресурсов игры
    }
};

}  // namespace game