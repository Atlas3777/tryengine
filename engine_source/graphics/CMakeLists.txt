#engine_source/graphics/CMakeLists.txt

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SANDBOX OFF CACHE BOOL "" FORCE)
set(BUILD_DOC OFF CACHE BOOL "" FORCE)

set(SDL3_TESTS OFF CACHE INTERNAL "")
set(SDL3_EXAMPLES OFF CACHE INTERNAL "")

FetchContent_Declare(SDL3 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git GIT_TAG main)
FetchContent_MakeAvailable(SDL3)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "include/engine/graphics/*.hpp")

add_library(engine_graphics STATIC ${SOURCES} ${HEADERS})
add_library(engine::graphics ALIAS engine_graphics)

target_include_directories(engine_graphics PUBLIC include PRIVATE src)

target_link_libraries(engine_graphics
        PUBLIC engine::core
        engine::resources
        SDL3::SDL3
)

add_library(tryengine_shared SHARED src/Module_Renderer.cpp)
target_link_libraries(tryengine_shared PRIVATE engine::core engine::graphics)

set_target_properties(tryengine_shared PROPERTIES
        # Клади собранный файл прямо в созданную нами папку модулей
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/modules/tryengine"
        # Подменяем расширение .so на .shared_module
        SUFFIX ".shared_module"
        PREFIX ""
        OUTPUT_NAME "tryengine"
)