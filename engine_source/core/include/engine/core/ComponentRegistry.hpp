#pragma once

#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <entt/entt.hpp>
#include <functional>
#include <string>
#include <vector>

namespace tryengine::core {

class ComponentRegistry {
public:
    template <typename T>
    void Register(const std::string& name) {
        // JSON сохранение
        json_serializers_.push_back([name](const entt::snapshot& snapshot, cereal::JSONOutputArchive& ar) {
            ar.setNextName(name.c_str());
            ar.startNode();  // Открываем узел с именем name
            snapshot.get<T>(ar);
            ar.finishNode();  // Закрываем его
        });

        // JSON загрузка
        json_deserializers_.push_back([name](entt::snapshot_loader& loader, cereal::JSONInputArchive& ar) {
            ar.setNextName(name.c_str());
            ar.startNode();
            loader.get<T>(ar);
            ar.finishNode();
        });

        // Сохраняем логику записи для Binary
        binary_serializers_.push_back(
            [](const entt::snapshot& snapshot, cereal::BinaryOutputArchive& ar) { snapshot.get<T>(ar); });

        // Сохраняем логику чтения для Binary
        binary_deserializers_.push_back(
            [](entt::snapshot_loader& loader, cereal::BinaryInputArchive& ar) { loader.get<T>(ar); });
    }

    // В методах Serialize / Deserialize для сущностей делаем так же:
    void Serialize(const entt::registry& reg, cereal::JSONOutputArchive& ar) const {
        entt::snapshot snapshot{reg};

        ar.setNextName("entities");
        ar.startNode();
        snapshot.get<entt::entity>(ar);
        ar.finishNode();

        for (const auto& fn : json_serializers_) {
            fn(snapshot, ar);
        }
    }

    void Deserialize(entt::registry& reg, cereal::JSONInputArchive& ar) const {
        reg.clear();
        entt::snapshot_loader loader{reg};

        ar.setNextName("entities");
        ar.startNode();
        loader.get<entt::entity>(ar);
        ar.finishNode();

        for (const auto& fn : json_deserializers_) {
            fn(loader, ar);
        }

        loader.orphans();

        // ar.finishNode();
    }

    // --- Binary Serialization ---

    void Serialize(const entt::registry& reg, cereal::BinaryOutputArchive& ar) const {
        entt::snapshot snapshot{reg};
        snapshot.get<entt::entity>(ar);
        for (const auto& fn : binary_serializers_) {
            fn(snapshot, ar);
        }
    }

    void Deserialize(entt::registry& reg, cereal::BinaryInputArchive& ar) const {
        reg.clear();
        entt::snapshot_loader loader{reg};
        loader.get<entt::entity>(ar);
        for (const auto& fn : binary_deserializers_) {
            fn(loader, ar);
        }
        loader.orphans();
    }

private:
    using JsonSaveFn = std::function<void(entt::snapshot&, cereal::JSONOutputArchive&)>;
    using JsonLoadFn = std::function<void(entt::snapshot_loader&, cereal::JSONInputArchive&)>;
    using BinSaveFn = std::function<void(entt::snapshot&, cereal::BinaryOutputArchive&)>;
    using BinLoadFn = std::function<void(entt::snapshot_loader&, cereal::BinaryInputArchive&)>;

    std::vector<JsonSaveFn> json_serializers_;
    std::vector<JsonLoadFn> json_deserializers_;
    std::vector<BinSaveFn> binary_serializers_;
    std::vector<BinLoadFn> binary_deserializers_;
};

}  // namespace tryengine::core