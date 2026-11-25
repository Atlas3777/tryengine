# UI textures

Sprites and icons the game UI draws with `Ui::ImageTexture`. `logo.png` here is a placeholder the
sample HUD uses; delete it (and the two guid constants described below) if your game has no UI
texture.

## Add a texture

1. Copy `YourImage.png` into this folder. PNG and JPG/JPEG are imported; other formats are not.
2. Start the editor. `TextureImporter` registers the extension, imports the file and writes
   `YourImage.png.meta` (the asset guid plus the bake settings) and the runtime artifact
   `game/artifacts/runtime/<guid>/0` in the `TextureBinary` format.
3. Use the guid from the `.meta` in the scripts, e.g. in `game/scripts/UiTheme.das`:

   ```das
   let public kLogoGuid : uint64 = 14429000468236573201ul
   ```

4. Add the same guid to `UiConfig::textures` in
   `engine_source/graphics/include/engine/graphics/UiSystem.hpp`. A build has to be told which
   UI-only textures to pack: nothing else references them, and a pak keeps no path table for
   imported assets, so a guid is the only reference that survives the build.
5. Rebuild the editor and the game client.

The editor's **Import Settings** panel (select the file in the Asset Browser) shows exactly what
the importer reads from the `.meta`, so "why is my texture compressed" is one click away.

## Import settings

```text
header: AssetMetaHeader = {
	guid: u64 = 14429000468236573201
	importer_type: string = "TextureImporter"
	sub_assets: u64[] = [
	]
}
settings: TextureImportSettings = {
	format: string = "rgba8_srgb"
	generate_mips: bool = false
	max_size: u32 = 0
	filter: string = "linear"
	wrap: string = "clamp"
}
```

| Field | Values | Notes |
| --- | --- | --- |
| `format` | `rgba8`, `rgba8_srgb`, `r8`, `rg8`, `bc1`, `bc1_srgb`, `bc3`, `bc3_srgb`, `bc7`, `bc7_srgb` | uncompressed for sprites; BC1 keeps only 1-bit alpha, BC3/BC7 for gradients |
| `generate_mips` | `true` / `false` | off for UI: a mip-blurred panel reads soft |
| `max_size` | `0` = source size | clamps the longest side, aspect kept |
| `filter` | `linear`, `nearest` | `nearest` for pixel art |
| `wrap` | `repeat`, `clamp`, `mirror` | `clamp` for sprites, `repeat` for tiling |

Editing the settings in the **Import Settings** panel and pressing **Save & Reimport** re-bakes
the texture without restarting the editor. An unknown value is an import error, not a silent
fallback.

## Check the bake without the editor

```bash
tools/agent/texture-probe.sh game/assets/ui/logo.png --meta game/assets/ui/logo.png.meta
```

The probe compiles the real `TextureBaker.cpp` + `TextureProcessor.cpp` and reads the result with
the runtime's `TextureBinary::Unpack`, so a texture that passes here imports into a valid
artifact. Without arguments it bakes a synthetic image and checks every format, the mip policy,
the size clamp and the sampler round trip.
