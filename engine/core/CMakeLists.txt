# engine/core/CMakeLists.txt

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "include/engine/core/*.hpp")

add_library(engine_core STATIC ${SOURCES} ${HEADERS})

target_include_directories(engine_core PUBLIC include PRIVATE src)

# Пробрасываем зависимости всем, кто подключит ядро
target_link_libraries(engine_core PUBLIC
        glm::glm
        EnTT::EnTT
        cereal::cereal
)