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
            ar.startNode();
            snapshot.get<T>(ar);
            ar.finishNode();
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

        if constexpr (requires(T& t, ResourceManager& rm) { t.Resolve(rm); }) {
            resolvers_.push_back([](entt::registry& reg, core::ResourceManager& rm, entt::entity target_entity) {
                if (target_entity == entt::null) {
                    // Резолвим всю сцену через each()
                    for (auto [entity, comp] : reg.view<T>().each()) {
                        comp.Resolve(rm);
                    }
                } else {
                    // Резолвим конкретную сущность (здесь reg не зависимый тип, поэтому try_get работает без template)
                    if (auto* comp = reg.try_get<T>(target_entity)) {
                        comp->Resolve(rm);
                    }
                }
            });
        }
    }

    void ResolveAll(entt::registry& reg, ResourceManager& rm, entt::entity target_entity = entt::null) const {
        for (const auto& resolve_fn : resolvers_) {
            resolve_fn(reg, rm, target_entity);
        }
    }

    // JSON:
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
    }

    // Binary Serialization
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

    using ResolveFn = std::function<void(entt::registry&, core::ResourceManager&, entt::entity)>;

    std::vector<JsonSaveFn> json_serializers_;
    std::vector<JsonLoadFn> json_deserializers_;
    std::vector<BinSaveFn> binary_serializers_;
    std::vector<BinLoadFn> binary_deserializers_;

    std::vector<ResolveFn> resolvers_;  // Храним резолверы
};

}  // namespace tryengine::core