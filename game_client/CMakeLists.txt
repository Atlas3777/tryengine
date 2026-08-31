file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "include/*.hpp")

add_executable(game_client ${SOURCES} ${HEADERS})

target_include_directories(game_client PRIVATE include)

target_link_libraries(game_client PRIVATE
        engine::core
        engine::resources
        engine::async
        engine::graphics
)