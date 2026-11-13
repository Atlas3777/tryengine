# AGENTS.md

Rules for AI agents working in this repository. This file is loaded automatically at the
start of every session whose working directory is inside this checkout. Keep it short; the
task-specific depth lives in `.dsh/skills/` and is loaded on demand by the `skill` tool.

## 1. What this is

`tryengine` is a small 3D engine on C++23: ECS via EnTT, rendering via SDL3 GPU (Vulkan
back end), editor UI via ImGui docking, gameplay and editor logic in daslang 0.6.5. Assets
are preprocessed into custom binary artifacts. Development platform is Fedora Linux; the
build is CMake + Ninja with GCC 15, `ccache` and `mold`.

## 2. Layout

| Path | Contents |
| --- | --- |
| `engine_source/core` | `Engine` (system registry), `Result`, logging, script system, script pak |
| `engine_source/graphics` | SDL GPU device, render graph, passes, loaders, `Module_Renderer` binding |
| `engine_source/platform` | SDL window/input, `Module_Input` binding |
| `engine_source/resources` | pak/artifact loading, resource manager, `Module_Resources` binding |
| `engine_source/async` | coroutine tasks, thread pool, `Module_AsyncFileManager` binding |
| `engine_source/daslang` | daslang sources of the engine API (`require tryengine/...`) |
| `editor/src`, `editor/include` | editor application, ImGui panels, asset pipeline, importers |
| `editor/daslang` | editor scripting (`require tryeditor/...`), entry point `EntryPoint.das` |
| `game_client` | runtime application (loads `./script_pak`, does not compile `.das`) |
| `game_server` | headless placeholder; not added to the build yet |
| `game/scripts`, `game/assets`, `game/artifacts` | game logic and content |
| `engine_content` | engine-shipped content and its artifacts |
| `modules/` | generated daslang module descriptors and shared modules |
| `cmake/`, `third_party_src/` | CPM package cache and CMake glue (never edit the cache) |

## 3. Build

Two configurations only: `cmake-build-debug` (Debug) and `cmake-build-release` (Release).
They share the CPM source cache in `third_party_src/` and the `ccache` cache.

```bash
tools/agent/build.sh editor                 # Debug editor (default)
tools/agent/build.sh game_client --release  # Release game client
tools/agent/build.sh modules                # daslang binding modules, always Release
tools/agent/build.sh all                    # editor + game_client in one config
```

Manual equivalent of one target:

```bash
cmake --build cmake-build-debug --target editor -j "$(nproc)"
```

Hard rules:

- Use `cmake --build <dir> --target <target>`. **Never** run `ninja -C cmake-build-*` from
  `PATH`: CLion configures those directories with its own bundled ninja, and a different
  ninja version restarts the build log (`build log version is too old; starting over`),
  which forces rebuilds for the human.
- Do not create a third configuration, and do not delete, move or reconfigure the two that
  exist. Do not pass `-D` options to an existing build directory unless the task is about
  the build itself.
- Never edit `third_party_src/` (shared CPM source cache) or anything under `cmake-build-*`.
- Binaries are `<build-dir>/bin/editor` and `<build-dir>/bin/game_client`. Run them from the
  repository root: `project.das_project`, `script_pak`, `content/`, `game/artifacts` are
  opened by relative path. Both are GUI applications and need a display; there is no
  headless mode.
- Agent builds use a workspace-local ccache directory (`.cache/ccache`) because the sandbox
  cannot write to CLion's `~/.cache/ccache`. The first build with an empty cache compiles
  everything; override the location with `TRY_CCACHE_DIR` and the parallelism with
  `TRY_JOBS` (default: `nproc`, capped at 8).

### daslang binding modules (`tryengine_shared`, `tryeditor_shared`)

- Build them in **Release only**. Both configurations write the same output file
  `modules/<name>/<name>.shared_module`; a Debug build overwrites the Release module, and
  then the editor and the daslang CLI load a debug binary.
- Rebuild after changing binding code: `engine_source/**/Module_*.cpp`,
  `editor/src/Module_*.cpp`, `DECLARE_MODULE`/`PULL_MODULE` lists, or any `addExtern` /
  type registration exposed to `.das`.
- The shared modules and their `.das_module.manifest` are generated and not tracked. After a
  fresh clone run `tools/agent/build.sh modules` once.

## 4. Verifying a change

One entry point:

```bash
tools/agent/verify.sh            # format + syntax + daslang gate + build of affected targets
tools/agent/verify.sh --fast     # skip builds
tools/agent/verify.sh --tidy     # also run clang-tidy on changed translation units
```

### The `.das` gate

```bash
daslang -dry-run -no-lint \
  -project project.das_project \
  -load_module "$PWD/modules/tryengine" \
  -load_module "$PWD/modules/tryeditor" \
  -load_module /opt/daslang/modules/dasLLVM \
  <file.das>
```

Why every flag matters:

- `-project project.das_project` resolves `tryengine/`, `tryeditor/`, `trygame/`, `imgui/`,
  `llvm/`, `daslib/` module paths; without it the project does not compile at all.
- `-load_module` is still required: native modules (`tryInput`, `tryRenderer`, ...) are
  registered by `modules/*/.das_module`, and `-project` alone yields
  `error[20605] missing prerequisite 'tryInput'`.
- `/opt/daslang/modules/dasLLVM` is required because `engine_source/daslang/Engine.das`
  requires `daslib/just_in_time`; without it the simulate step panics with
  `LLVM JIT: the emitter module llvm/daslib/llvm_macro did not load`.
- `-dry-run` compiles and simulates (stricter than `-compile-only`) without running the code.
- Check the exit code **and** `error[` in the output: daslang can print errors and exit 0.
- `daslang -tool lint <file>` cannot resolve this project's native modules (it reports
  `SKIP ... missing prerequisite`) and `-project` breaks the lint tool itself. Lint through
  the MCP tool, or skip linting.

### `script_pak`

`game_client` does not compile scripts: it loads `./script_pak`, which the editor builds on
`Z` (test-compile on `X`). A `.das` change does not reach the game until the pak is rebuilt
in the GUI. `tools/agent/editor-check.sh` runs the editor with a timeout and greps its
console for `error[`; it compiles the same sources as the CLI gate, but needs a display.

## 5. C++ rules

- Exceptions and RTTI are disabled (`-fno-rtti -fno-exceptions`). Do not add `throw`, `try`
  or `dynamic_cast`.
- Containers and strings come from EASTL (`eastl::vector`, `eastl::string`, ...). `std` is
  used only where it already is (`std::expected`, `std::unique_ptr`, `std::move`).
- Errors are values: `tryengine::Result<T>` (`std::expected<T, core::Error>`), `LogError`,
  `LogInfo`, `TRY_ASSERT`, `TRY_INLINE`, `LogAndMakeError`. Do not add error-code out
  parameters.
- Systems: define a class, register it with `engine.RegisterSystem<T>(args...)`, read it
  back with `engine.Get<T>()`. Members are `snake_case_`, methods and types are
  `PascalCase`, free functions are `PascalCase`.
- Headers go to `<lib>/include/engine/<lib>/*.hpp`, sources to `<lib>/src/*.cpp`, includes
  are spelled `"engine/core/Engine.hpp"`.
- `engine_core` must not depend on Vulkan, SDL GPU or ImGui: the headless server links it.
- Style is machine-enforced: `.clang-format` (Google, 120 columns, 4 spaces, left
  pointers) and `.clang-tidy` (bugprone and performance, readability disabled).

## 6. daslang rules

- gen2 syntax (braces). New files open with `options gen2`; never write gen1.
- Module resolution is `project.das_project` -> `module_get`: `tryengine/...` maps to
  `engine_source/daslang`, `tryeditor/...` to `editor/daslang`, `trygame/...` to
  `game/scripts`, plus `imgui/`, `llvm/`, `daslib/`, `live/`. `DAS_PAK_ROOT` is absolute.
- Entry points are `[export] def Name()` invoked by name from C++ through
  `ScriptSystem::InvokeFunctionFast/Safe`; renaming one silently breaks the C++ side.
- ECS access in scripts goes through the DECS macros (`create_entity`,
  `apply_decs_template`), see `engine_source/daslang/EcsMacro.das` and
  `game/scripts/Game.das`.
- Formatting: `das-fmt --verify` (or `das-fmt` to rewrite). Lint: MCP `lint`.

## 7. Code style

- Comments are written in Russian in this repository; match the file you edit instead of
  switching its language.
- Do not leave commented-out code, debug prints or TODO placeholders.
- Follow the layer and pattern that already exists; search the repository before inventing a
  parallel mechanism.
- Long-form conventions: `.dsh/skills/tryengine-cpp/SKILL.md` and
  `.dsh/skills/tryengine-daslang/SKILL.md`.

## 8. Boundaries

Never modify or commit: `third_party_src/`, `cmake-build-*/`, `.idea/`, `.zed/`,
`script_pak`, `content/`, `engine_content/artifacts`, `game/artifacts`,
`*.shared_module`, `*.das_module.manifest`, `.cache/`.

Never run: `git push --force`, `git reset --hard` on a shared branch, `rm -rf` outside
generated directories, `cmake -B` on an existing build directory, or a formatter over the
whole repository as a drive-by change.

Git: work on `agent/<task>`, keep commits small, commit only the files you changed. No
drive-by refactors, mass reformatting, dependency bumps or unrelated cleanups. Do not commit
on `main` unless the user asked for it.

Stop and ask when: the task needs a new dependency, a `CMakeLists.txt` change, a public API
or `[export]` rename, or when the result cannot be verified with the commands above.

## 9. Definition of done

1. `tools/agent/verify.sh` exits 0, or the report explains which step does not apply.
2. `git status --short` shows only the files you intended to change.
3. Report what changed, which commands ran, and what was **not** verified (GUI, GPU output,
   `script_pak`, anything needing a display).
4. If `.das` changes must reach the game client, state that `script_pak` needs a rebuild in
   the editor (`Z`).

## 10. Toolkit

- `tools/agent/` — `doctor.sh` (environment check), `build.sh`, `das-check.sh`,
  `editor-check.sh`, `fmt.sh`, `tidy.sh`, `verify.sh`.
- `.dsh/skills/` — `tryengine-build`, `tryengine-daslang`, `tryengine-cpp`.
- MCP server `tryengine-daslang` — `lint`, `cpp_compile_check`, `cpp_build_info`,
  `cpp_grep_usage`, `find_symbol`, `format_file`. Its `compile_check` and `list_*` tools fail
  on engine scripts (simulate panic, see section 4); use the CLI gate for compilation.
