file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")

add_executable(game_server ${SOURCES})

# Притягиваем ТОЛЬКО игру и ядро. Никакого SDL3!
target_link_libraries(game_server PRIVATE game_static engine_core)