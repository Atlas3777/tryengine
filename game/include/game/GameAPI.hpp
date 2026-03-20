#pragma once

// Вперед-декларации (forward declarations), чтобы не тащить тяжелые хедеры
namespace engine {
class Engine;
}

// 1. Указатели на функции (типы для редактора)
typedef void (*UpdateGameSystemsFn)(engine::Engine*);

// 2. Глобальный блок для экспорта из SO
// Это заставит компилятор создать символы именно с такими именами, без префиксов
extern "C" {
  void UpdateGameSystems(engine::Engine* engine);
}

// 3. Структура для хранения загруженной библиотеки
struct GameLibrary {
  void* handle = nullptr;
  UpdateGameSystemsFn updateGameSystems = nullptr;

  bool IsValid() const {
    return handle != nullptr && updateGameSystems != nullptr;
  }
};