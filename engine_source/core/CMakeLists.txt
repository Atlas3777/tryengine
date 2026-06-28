# engine_source/core/CMakeLists.txt

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SANDBOX OFF CACHE BOOL "" FORCE)
set(BUILD_DOC OFF CACHE BOOL "" FORCE)

FetchContent_Declare(entt     GIT_REPOSITORY https://github.com/skypjack/entt.git   GIT_TAG v3.16.0)
FetchContent_Declare(glm      GIT_REPOSITORY https://github.com/g-truc/glm.git      GIT_TAG master)
FetchContent_MakeAvailable(entt glm)

find_package(DAS REQUIRED)
find_package(Threads REQUIRED)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "include/engine/core/*.hpp")

add_library(engine_core STATIC ${SOURCES} ${HEADERS})

set_target_properties(engine_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(engine_core PUBLIC include PRIVATE src)
target_link_libraries(engine_core PUBLIC
        glm::glm
        EnTT::EnTT
        cereal::cereal
        DAS::libDaScriptDyn
        Threads::Threads
)

get_target_property(DAS_INC_DIRS DAS::libDaScriptDyn INTERFACE_INCLUDE_DIRECTORIES)
list(GET DAS_INC_DIRS 0 DAS_MAIN_INCLUDE_DIR)
cmake_path(GET DAS_MAIN_INCLUDE_DIR PARENT_PATH DAS_SDK_ROOT)

target_compile_definitions(engine_core PUBLIC
        DAS_MOD_EXPORTS
        "DAS_ROOT_DIR=\"${DAS_SDK_ROOT}\""
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(src/ScriptSystem.cpp
            src/Module_Renderer.cpp
            PROPERTIES COMPILE_OPTIONS "-fno-rtti"
    )
endif()

add_library(tryengine_shared SHARED src/Module_Renderer.cpp)
target_link_libraries(tryengine_shared PRIVATE engine_core DAS::libDaScriptDyn)

set_target_properties(tryengine_shared PROPERTIES
        # Клади собранный файл прямо в созданную нами папку модулей
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/modules/tryengine"
        # Подменяем расширение .so на .shared_module
        SUFFIX ".shared_module"
        PREFIX ""
        OUTPUT_NAME "tryengine"
)

# Не забываем отключить RTTI и для этой цели
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(src/Module_Renderer.cpp
            PROPERTIES COMPILE_OPTIONS "-fno-rtti"
    )
endif()