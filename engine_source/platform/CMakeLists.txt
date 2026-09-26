#engine_source/platform/CMakeLists.txt

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "include/engine/platform/*.hpp")

add_library(engine_platform STATIC ${SOURCES} ${HEADERS})
add_library(engine::platform ALIAS engine_platform)

target_include_directories(engine_platform PUBLIC include PRIVATE src)

target_link_libraries(engine_platform PUBLIC
        engine::core
        engine::async
)
