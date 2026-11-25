// Camera block shared by shaders that take the view/projection through a BDA pointer.
//
// Shader modules are plain sources: they have no .meta, no guid and no artifact. The editor
// adds engine_content/shader_modules to the Slang session's search paths, so a shader imports
// this file as `import engine.common;`.
//
// Every declaration an importing shader uses must be public. Unlike a shader file compiled as
// a standalone module, a module's top-level declarations and struct members are internal by
// default.

public struct Camera
{
    public float4x4 view;
    public float4x4 proj;
};
