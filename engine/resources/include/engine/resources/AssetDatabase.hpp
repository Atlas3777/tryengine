#pragma once

#include <string>
#include <unordered_map>
#include <entt/core/fwd.hpp>

namespace engine::resources {
class AssetDatabase {
   public:
    std::string GetPath(entt::id_type id);
private:
    std::unordered_map<entt::id_type, std::string> m_idToPath;
};
}  // namespace engine::resources