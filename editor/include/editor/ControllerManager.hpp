#include <any>
#include <entt/core/type_info.hpp>
#include <unordered_map>

namespace tryeditor {

class ControllerManager {
public:
    template <typename T, typename... Args>
   void RegisterController(Args&&... args) {
        // Используем emplace или make_any, чтобы сконструировать объект T на месте
        controllers[entt::type_hash<T>::value()] = std::make_any<T>(std::forward<Args>(args)...);
    }

    template <typename T>
    [[nodiscard]] T& Get() {
        auto type_id = entt::type_hash<T>::value();
        auto it = controllers.find(type_id);

        assert(it != controllers.end() && "Controller not registered!");

        // any_cast к указателю вернет адрес объекта внутри any
        return *std::any_cast<T>(&it->second);
    }

private:
    std::unordered_map<entt::id_type, std::any> controllers;
};

}  // namespace tryeditor