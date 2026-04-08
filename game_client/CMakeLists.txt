file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")

add_executable(game_client ${SOURCES})

target_link_libraries(game_client PRIVATE game_static engine_graphics)