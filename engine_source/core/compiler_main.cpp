// #include "daScript/daScript.h"
// #include <iostream>
// #include <cstring>
//
// // Объявляем нашу функцию-зацеп из GameplayBindings.cpp
// extern void register_tryengine_bindings_stub();
//
// int main(int argc, char * argv[]) {
//     // Принудительно заставляем линкер включить биндинги движка в бинарник
//     register_tryengine_bindings_stub();
//
//     // Инициализируем рантайм daScript
//     das::Module::Initialize();
//     NEED_ALL_DEFAULT_MODULES;
//
//     if (argc < 2) {
//         std::cout << "Usage: daslang_tryengine <script.das> [options]\n";
//         das::Module::Shutdown();
//         return 1;
//     }
//
//     das::TextPrinter tout;
//     das::ModuleGroup libGroup;
//     auto fAccess = das::make_smart<das::FsFileAccess>();
//
//     std::string scriptPath = argv[1];
//
//     // Компиляция скрипта (здесь автоматически подтянутся вшитые модули)
//     auto program = das::compileDaScript(scriptPath, fAccess, tout, libGroup);
//     if (program->failed()) {
//         for (auto & err : program->errors) {
//             tout << das::reportError(err.at, err.what, err.extra, err.fixme, err.cerr);
//         }
//         das::Module::Shutdown();
//         return 1;
//     }
//
//     // Проверяем флаги компилятора. Расширение VS Code часто передает -check или --check
//     bool checkOnly = false;
//     for (int i = 2; i < argc; ++i) {
//         if (std::strcmp(argv[i], "-check") == 0 || std::strcmp(argv[i], "--check") == 0) {
//             checkOnly = true;
//         }
//     }
//
//     if (checkOnly) {
//         das::Module::Shutdown();
//         return 0; // Для проверки синтаксиса этого достаточно
//     }
//
//     // Симуляция контекста (нужна, если VS Code запускает полноценный language_server.das)
//     das::Context ctx(program->getContextStackSize());
//     if (!program->simulate(ctx, tout)) {
//         std::cerr << "Simulation failed\n";
//         das::Module::Shutdown();
//         return 1;
//     }
//
//     // Пробуем запустить main, если скрипт предназначен для выполнения
//     auto fnMain = ctx.findFunction("main");
//     if (!fnMain) {
//         std::cerr << "Function 'main' not found in script\n";
//         das::Module::Shutdown();
//         return 1;
//     }
//
//     ctx.restart();
//     ctx.evalWithCatch(fnMain, nullptr);
//
//     das::Module::Shutdown();
//     return 0;
// }