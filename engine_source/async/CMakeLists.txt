find_package(Threads REQUIRED)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "include/engine/async/*.hpp")

if (SOURCES)
    add_library(engine_async STATIC ${SOURCES} ${HEADERS})

    target_include_directories(engine_async
            PUBLIC include
            PRIVATE src
    )

    target_link_libraries(engine_async PUBLIC
            Threads::Threads
            engine::core
            uring
    )
else ()
    add_library(engine_async INTERFACE)
    target_include_directories(engine_async INTERFACE include)
    target_link_libraries(engine_async INTERFACE
            Threads::Threads
            engine::core
            uring
    )
endif ()

add_library(engine::async ALIAS engine_async)