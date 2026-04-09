cmake_minimum_required(VERSION 3.31)
project(tryengine)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Choose the type of build" FORCE)
endif()

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(SRC_LINK "${CMAKE_SOURCE_DIR}/compile_commands.json")
set(BIN_FILE "${CMAKE_BINARY_DIR}/compile_commands.json")
if(UNIX)
    execute_process(COMMAND ${CMAKE_COMMAND} -E rm -f "${SRC_LINK}")
    execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink "${BIN_FILE}" "${SRC_LINK}")
endif()

# Опционально ASAN
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND USE_ASAN)
    message(STATUS "ASAN IS ENABLED")
    set(ASAN_FLAGS "-fsanitize=address" "-fno-omit-frame-pointer" "-fsanitize=undefined")
    add_compile_options(${ASAN_FLAGS})
    add_link_options(${ASAN_FLAGS})
endif()

# ==================== МОДУЛИ ====================
add_subdirectory(engine)
add_subdirectory(game)
add_subdirectory(editor)
add_subdirectory(game_client)