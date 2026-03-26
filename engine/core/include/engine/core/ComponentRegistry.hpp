#pragma once

#include <entt/entt.hpp>
#include <cereal/archives/binary.hpp>
#include <functional>
#include <vector>

namespace engine::core {

class ComponentRegistry {
public:
  // Сигнатуры функций, которые принимают уже созданные архивы и снапшоты
  using SaveCallback = std::function<void(entt::snapshot&, cereal::BinaryOutputArchive&)>;
  using LoadCallback = std::function<void(entt::snapshot_loader&, cereal::BinaryInputArchive&)>;

  // Метод для регистрации любого компонента T
  template<typename T>
  void RegisterForSerialization() {
    // Запоминаем, как сохранять тип T
    save_callbacks_.push_back([](entt::snapshot& snap, cereal::BinaryOutputArchive& ar) {
        snap.get<T>(ar);
    });

    // Запоминаем, как загружать тип T
    load_callbacks_.push_back([](entt::snapshot_loader& loader, cereal::BinaryInputArchive& ar) {
        loader.get<T>(ar);
    });
  }

  // Вызывается при сохранении сцены
  void Serialize(entt::registry& reg, cereal::BinaryOutputArchive& ar) {
    entt::snapshot snap{reg};
    snap.get<entt::entity>(ar); // Сущности всегда сохраняем первыми!

    for (auto& callback : save_callbacks_) {
      callback(snap, ar);     // Вызываем .get<T>(ar) для всех зарегистрированных типов
    }
  }

  // Вызывается при загрузке сцены
  void Deserialize(entt::registry& reg, cereal::BinaryInputArchive& ar) {
    entt::snapshot_loader loader{reg};
    loader.get<entt::entity>(ar);

    for (auto& callback : load_callbacks_) {
      callback(loader, ar);
    }
    loader.orphans(); // Удаляем пустые сущности, если что-то пошло не так
  }

private:
  std::vector<SaveCallback> save_callbacks_;
  std::vector<LoadCallback> load_callbacks_;
};

} // namespace engine