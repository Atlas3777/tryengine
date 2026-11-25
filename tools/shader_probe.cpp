// Headless compile check for Slang shaders and their modules.
//
// Compiles every shader through the real editor ShaderCompilerSystem (the same object the
// importer uses) and requires both stages to produce SPIR-V. Shader modules are ordinary
// sources, so nothing is imported or written here: the probe only verifies that the source
// tree resolves its `import engine.*` / `import game.*` dependencies.
//
// Run through tools/agent/shader-probe.sh; it builds this file against the editor's engine_core
// and the bundled Slang library.

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "editor/shader/ShaderCompilerSystem.hpp"
#include "engine/graphics/MaterialConfig.hpp"
#include "engine/resources/ConfigFormat.hpp"
#include "engine/resources/binary/MeshBinary.hpp"

namespace {

bool ReadFile(const char* path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
        return false;
    std::ostringstream stream;
    stream << file.rdbuf();
    out = stream.str();
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: shader-probe <shader.slang>...\n");
        return 2;
    }

    tryeditor::ShaderCompilerSystem compiler(tryeditor::ShaderCompilerConfig{
        .module_search_paths = {"engine_content/shader_modules", "game/shader_modules"},
        .context_count = 1,
    });

    bool ok = true;
    for (int i = 1; i < argc; ++i) {
        const char* path = argv[i];
        std::string source;
        if (!ReadFile(path, source)) {
            std::printf("FAIL %s: cannot read\n", path);
            ok = false;
            continue;
        }

        tryeditor::ShaderCompileRequest request{};
        request.path = path;
        request.source = source.c_str();

        auto result = compiler.Compile(request);
        if (!result.has_value()) {
            std::printf("FAIL %s: %s\n", path, result.error().Message());
            ok = false;
            continue;
        }

        if (result->shader.vertex_spv.empty() || result->shader.fragment_spv.empty()) {
            std::printf("FAIL %s: missing SPIR-V stage (vs=%zu fs=%zu)\n", path, result->shader.vertex_spv.size(),
                        result->shader.fragment_spv.size());
            ok = false;
            continue;
        }

        // The material schema is the editor's view of the same reflection. A malformed
        // ConfigFormat here would only show up as an empty material inspector at runtime.
        const eastl::string schema = tryengine::graphics::BuildMaterialSchemaConfig(result->shader.reflection);
        const auto parsed_schema = tryengine::resources::ParseConfig(schema);
        if (!parsed_schema.has_value()) {
            const auto message = parsed_schema.error().Message();
            std::printf("FAIL %s: material schema does not parse: %.*s\n", path, static_cast<int>(message.size()),
                        message.data());
            ok = false;
            continue;
        }
        bool has_attributes = false;
        for (const tryengine::resources::ConfigField& field : parsed_schema->fields)
            if (field.name == "attributes")
                has_attributes = true;
        if (!has_attributes) {
            std::printf("FAIL %s: material schema has no attributes field\n", path);
            ok = false;
            continue;
        }

        // The .tshd artifact is the only thing the runtime sees: check that the new
        // reflection field survives Pack/Unpack, not just the in-memory compile result.
        const eastl::vector<uint8_t> packed = tryengine::graphics::PackShaderBinary(result->shader);
        const auto unpacked = tryengine::graphics::UnpackShaderBinary(packed);
        if (!unpacked.has_value() ||
            unpacked->reflection.required_attributes != result->shader.reflection.required_attributes) {
            std::printf("FAIL %s: shader binary round-trip lost required_attributes\n", path);
            ok = false;
            continue;
        }

        std::printf(
            "OK   %s: vs=%zu B fs=%zu B deps=%zu attrs=[%s]\n", path, result->shader.vertex_spv.size(),
            result->shader.fragment_spv.size(), result->dependencies.size(),
            tryengine::resources::DescribeMeshAttributeFlags(result->shader.reflection.required_attributes).c_str());
        for (const tryeditor::ShaderDependency& dependency : result->dependencies)
            std::printf("       dep %s\n", dependency.path.c_str());
    }

    return ok ? 0 : 1;
}
