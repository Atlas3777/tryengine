# game/CMakeLists.txt

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "include/game/*.hpp")

add_library(game_obj OBJECT ${SOURCES} ${HEADERS})
set_target_properties(game_obj PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(game_obj PUBLIC include PRIVATE src)

# Только ядро движка
target_link_libraries(game_obj PUBLIC engine_core)

# .so для редактора
add_library(game_shared SHARED $<TARGET_OBJECTS:game_obj>)
target_include_directories(game_shared PUBLIC include)
target_link_libraries(game_shared PUBLIC engine_core)
set_target_properties(game_shared PROPERTIES OUTPUT_NAME "game")

# .a для клиента/сервера
add_library(game_static STATIC $<TARGET_OBJECTS:game_obj>)
target_include_directories(game_static PUBLIC include)
target_link_libraries(game_static PUBLIC engine_core)
