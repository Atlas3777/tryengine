# Game shaders

Slang shader sources for the game. A file here is an **asset**: it has a `.slang.meta` with a
guid, and `SlangImporter` compiles it into the runtime artifact `game/artifacts/runtime/<guid>/0`
(SPIR-V for both stages plus the reflection the material system reads).

Shared code does not belong here. Cut it into a module under `game/shader_modules/` (game code)
or `engine_content/shader_modules/` (engine code) and import it; see
[../shader_modules/README.md](../shader_modules/README.md).

## Add a shader

1. Put `YourShader.slang` here with both entry points:

   ```slang
   [shader("vertex")]
   YourVSOutput mainVS(uint vertexID : SV_VertexID, uint drawID : SV_DrawIndex) { ... }

   [shader("fragment")]
   float4 fsMain(YourVSOutput input) : SV_Target { ... }
   ```

   `vsMain`/`mainVS` and `fsMain`/`mainFS` are the names the compiler looks for; the shader
   must define both stages.

2. Start the editor. The first scan creates `YourShader.slang.meta` and writes the artifact.
   The Asset Browser shows the source under **Game Shaders**.

3. Reference the guid from a material (`game/data/materials/*.mat`) or, for an editor pass,
   from C++ (`ResourceManager::Get<Shader>(guid)`).

A shader with no entry points is rejected: shared code is a module, not an imported asset, and
modules never get a `.meta`, guid or artifact.
