#engine/graphics/CMakeLists.txt

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "include/engine/graphics/*.hpp")

add_library(engine_graphics STATIC ${SOURCES} ${HEADERS})

target_include_directories(engine_graphics PUBLIC include PRIVATE src)

target_link_libraries(engine_graphics
        PUBLIC engine_core engine_resources SDL3::SDL3
)