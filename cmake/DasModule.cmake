# Биндинги C++-модулей daslang.
#
# Каждый модуль собирается в собственную shared-библиотеку
# `modules/<name>/<name>.shared_module`. Рядом лежит дескриптор
# `modules/<name>/.das_module` — его читает daslang (`-load_module` и скан `modules/`),
# поэтому дескриптор хранится в git, а не генерируется.
#
# В приложениях (editor, game_client) те же .cpp линкуются статически: модули
# регистрируются через `PULL_MODULE` в `*ScriptSetup.hpp`. Shared-копии нужны только
# внешнему daslang (CLI-гейт, язык, редакторские скрипты вне приложения).
#
#   tryengine_add_das_module(<target> <name> <source> [<lib> ...])
#
# Чтобы пересобрать все биндинги сразу, есть агрегирующая цель `das_modules`
# (см. `tryengine_add_das_module_all`).

function(tryengine_add_das_module target module_name source)
    set(module_dir "${CMAKE_SOURCE_DIR}/modules/${module_name}")
    if (NOT EXISTS "${module_dir}/.das_module")
        message(FATAL_ERROR "Нет дескриптора daslang-модуля: modules/${module_name}/.das_module")
    endif()

    add_library(${target} SHARED "${source}")
    if (ARGC GREATER 3)
        target_link_libraries(${target} PRIVATE ${ARGN})
    endif()

    set_target_properties(${target} PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY "${module_dir}"
            SUFFIX ".shared_module"
            PREFIX ""
            OUTPUT_NAME "${module_name}"
    )

    set_property(GLOBAL APPEND PROPERTY TRY_DAS_MODULE_TARGETS ${target})
    message(STATUS "daslang module: ${module_name} -> modules/${module_name}")
endfunction()

# Агрегирующая цель: `cmake --build <dir> --target das_modules` собирает все биндинги,
# зарегистрированные выше. Вызывать после всех add_subdirectory.
function(tryengine_add_das_module_all)
    get_property(targets GLOBAL PROPERTY TRY_DAS_MODULE_TARGETS)
    add_custom_target(das_modules DEPENDS ${targets})
endfunction()
