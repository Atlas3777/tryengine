#pragma once

// Вперед-декларации (forward declarations), чтобы не тащить тяжелые хедеры
namespace tryengine {}  // namespace tryengine
namespace tryengine::core {
class Engine;
}

// 1. Указатели на функции (типы для редактора)
typedef void (*UpdateGameSystemsFn)(tryengine::core::Engine*);

// 2. Глобальный блок для экспорта из SO
// Это заставит компилятор создать символы именно с такими именами, без префиксов
extern "C" {
void UpdateGameSystems(tryengine::core::Engine* engine);
}

// 3. Структура для хранения загруженной библиотеки
struct GameLibrary {
    void* handle = nullptr;
    UpdateGameSystemsFn updateGameSystems = nullptr;

    bool IsValid() const { return handle != nullptr && updateGameSystems != nullptr; }
};