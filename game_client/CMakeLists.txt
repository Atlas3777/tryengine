file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "include/*.hpp")

add_executable(game_client ${SOURCES} ${HEADERS})

set_target_properties(game_client PROPERTIES
        BUILD_RPATH   "$ORIGIN/lib;$ORIGIN"
        INSTALL_RPATH "$ORIGIN/lib;$ORIGIN"
        BUILD_WITH_INSTALL_RPATH ON
)

target_include_directories(game_client PRIVATE include)

target_link_libraries(game_client PRIVATE
        engine::core
        engine::resources
        engine::async
        engine::graphics
)