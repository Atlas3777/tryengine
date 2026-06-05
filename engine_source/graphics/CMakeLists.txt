#engine_source/graphics/CMakeLists.txt

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SANDBOX OFF CACHE BOOL "" FORCE)
set(BUILD_DOC OFF CACHE BOOL "" FORCE)

set(SDL3_TESTS OFF CACHE INTERNAL "")
set(SDL3_EXAMPLES OFF CACHE INTERNAL "")

FetchContent_Declare(SDL3     GIT_REPOSITORY https://github.com/libsdl-org/SDL.git  GIT_TAG main)
FetchContent_MakeAvailable(SDL3)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "include/engine/graphics/*.hpp")

add_library(engine_graphics STATIC ${SOURCES} ${HEADERS})

target_include_directories(engine_graphics PUBLIC include PRIVATE src)

target_link_libraries(engine_graphics
        PUBLIC engine_core engine_resources SDL3::SDL3
)