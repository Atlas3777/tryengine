#pragma once

#include <EASTL/string.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/vector.h>
#include <atomic>
#include <cstdint>

#include "engine/core/Result.hpp"
#include "engine/graphics/ShaderBinary.hpp"

namespace slang {
struct IGlobalSession;
}  // namespace slang

namespace tryeditor {

struct ShaderCompilerConfig {
    // Каталоги, в которых Slang ищет модули для `import` (порядок важен: первый найденный
    // выигрывает). Каталог исходника добавляется автоматически, поэтому соседние модули
    // находятся без настройки.
    //
    // Шейдерные модули — обычные исходники без .meta/guid/артефакта. Они не входят ни в
    // один asset-домен и видны только здесь; шейдеры импортируют их как `engine.geometry`
    // и `game.<имя>`.
    eastl::vector<eastl::string> module_search_paths;

    // Количество независимых компиляторных контекстов. IGlobalSession в Slang не
    // thread-safe, поэтому каждый контекст сериализует работу своим мьютексом; несколько
    // контекстов дают параллельную компиляцию разных ассетов.
    uint32_t context_count = 2;
};

struct ShaderDependency {
    eastl::string name;  // имя модуля (для include-файлов — stem имени файла)
    eastl::string path;  // путь файла, по возможности относительно cwd
};

struct ShaderCompileRequest {
    eastl::string path;  // путь исходника для диагностики и относительного поиска модулей
    eastl::string source;
};

struct ShaderCompileResult {
    // SPIR-V обеих стадий, отфильтрованная рефлексия и счётчики.
    tryengine::graphics::ShaderBinaryContent shader;

    // Текстовый дамп для editor-артефакта.
    eastl::string reflection_dump;

    // Всё, что компиляция реально прочитала из каталогов модулей: import-модули и include-файлы.
    // AssetPipeline сохраняет это в штамп, чтобы правка модуля переимпортировала зависимые шейдеры.
    eastl::vector<ShaderDependency> dependencies;
};

// Долгоживущий компилятор Slang. Один экземпляр на приложение (регистрируется в Engine), но не
// синглтон: импортёр получает ссылку при регистрации. Global session создаётся один раз на
// контекст, а не на каждый .slang-файл; сессия компиляции — короткоживущая и изолирует
// кэш модулей одного запроса от другого.
class ShaderCompilerSystem {
public:
    explicit ShaderCompilerSystem(ShaderCompilerConfig config);
    ~ShaderCompilerSystem();

    ShaderCompilerSystem(const ShaderCompilerSystem&) = delete;
    ShaderCompilerSystem& operator=(const ShaderCompilerSystem&) = delete;

    // Потокобезопасен: запросы распределяются по контекстам и сериализуются внутри каждого.
    [[nodiscard]] tryengine::Result<ShaderCompileResult> Compile(const ShaderCompileRequest& request);

private:
    struct CompilerContext;

    ShaderCompilerConfig config_;
    eastl::vector<eastl::unique_ptr<CompilerContext>> contexts_;
    std::atomic<uint32_t> next_context_{0};
};

}  // namespace tryeditor
