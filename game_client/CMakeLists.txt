# game_client/CMakeLists.txt

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")

add_executable(game_client ${SOURCES})

# Линкуем статическую игру, она по цепочке притянет engine и все зависимости (SDL3 и т.д.)
target_link_libraries(game_client PRIVATE game_static)
