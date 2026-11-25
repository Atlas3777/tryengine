# Shader modules

Reusable Slang code for shaders, with no asset identity: a file here has **no `.meta`, no guid
and no artifact**. The editor only adds `engine_content/shader_modules` and
`game/shader_modules` to the Slang session search paths, so a shader pulls a module in at
compile time.

Layout is the module namespace:

| File | Import |
| --- | --- |
| `game/shader_modules/game/common.slang` | `import game.common;` |
| `engine_content/shader_modules/engine/geometry.slang` | `import engine.geometry;` |

A game shader can import engine modules; engine shaders must not import game modules.

## Write a module

1. Put the file under the namespace folder (`game/` here, `engine/` in `engine_content`).
2. Mark everything the importing shader uses as `public` — including struct fields. Unlike a
   shader file, a module's top-level declarations and struct members are internal by default:

   ```slang
   public struct Camera
   {
       public float4x4 view;
       public float4x4 proj;
   };
   ```

3. Edit the shader and add `import game.common;` (or the engine namespace). Imports are not
   transitive: a shader that uses a type from `engine.geometry` imports `engine.geometry`
   directly, even when another imported module already does.

Modules are tracked as import dependencies of the shader stamp, so editing a module
re-imports every shader that read it. They are not shown in the Asset Browser and are not
packed: only the compiled shader artifact is.

## Mesh attribute contract

Every shader must declare the mesh attributes its vertex stage reads:

```slang
public static const uint MESH_ATTRIBUTES = ATTR_BIT_NORMAL | ATTR_BIT_TEXCOORD0;
```

`0` means positions only; `0xFFFFFFFEu` marks a shader that is not a mesh material at all
(UI, debug/editor passes). The editor compares the mask with `Mesh::attribute_flags` and only
offers a material for a mesh that has every requested attribute. `ATTR_BIT_*` constants come
from `engine.geometry`. The mask is stored in the shader artifact and in the material schema,
so changing it requires a shader reimport — the editor does that automatically when the
source changes.

The engine already ships `engine.common`, `engine.geometry` and `engine.opaque`; prefer
importing them over copying the vertex fetch or the opaque push-constant layout.
