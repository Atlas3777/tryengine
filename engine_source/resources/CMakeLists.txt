#engine_source/resources/CMakeLists.txt

FetchContent_Declare(glaze GIT_REPOSITORY https://github.com/stephenberry/glaze.git GIT_TAG v7.8.4)
FetchContent_MakeAvailable(glaze)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "include/engine/resources/*.hpp")

add_library(engine_resources STATIC ${SOURCES} ${HEADERS})
add_library(engine::resources ALIAS engine_resources)

target_include_directories(engine_resources PUBLIC include PRIVATE src)

target_link_libraries(engine_resources PUBLIC
        engine::core
        engine::async
        glaze::glaze
)
