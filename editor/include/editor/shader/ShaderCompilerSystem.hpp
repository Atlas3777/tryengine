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

enum class ShaderAssetKind : uint8_t {
    Shader,  // есть вершинная и фрагментная точки входа: результат — SPIR-V + рефлексия
    Module,  // точек входа нет: результат — сериализованный модуль
};

struct ShaderCompileRequest {
    eastl::string path;  // путь исходника для диагностики и относительного поиска модулей
    eastl::string source;
};

struct ShaderCompileResult {
    ShaderAssetKind kind = ShaderAssetKind::Shader;

    // Заполняется для Shader: SPIR-V обеих стадий, отфильтрованная рефлексия и счётчики.
    tryengine::graphics::ShaderBinaryContent shader;

    // Заполняется для Shader: текстовый дамп для editor-артефакта.
    eastl::string reflection_dump;

    // Заполняется для Module: сериализованный checked-модуль Slang (ISession::loadModuleFromIRBlob).
    eastl::vector<uint8_t> module_ir;
    eastl::string module_name;

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
