#pragma once

#include <entt/entt.hpp>
#include <string>

namespace tryengine::core {

class Scene {
public:
    explicit Scene(const std::string& name = "Untitled Scene") : name_(name) {
        registry_ = std::make_unique<entt::registry>();
    };
    ~Scene() = default;

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    Scene(Scene&&) = default;
    Scene& operator=(Scene&&) = default;

    void Rename(const std::string& name) { name_ = name; };
    void SetAssetID(const uint64_t id) { asset_id_ = id; }


    [[nodiscard]] uint64_t GetAssetID() const { return asset_id_; }
    [[nodiscard]] const std::string& GetName() const { return name_; }
    [[nodiscard]] bool IsPersistent() const { return asset_id_ != 0; }
    [[nodiscard]] entt::registry& GetRegistry() const { return *registry_; }

private:
    std::string name_;
    uint64_t asset_id_ = 0;
    std::unique_ptr<entt::registry> registry_;
};

}  // namespace tryengine::core