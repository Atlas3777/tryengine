# UI font

The game UI draws text with one baked font. Put a `.ttf` here (Latin + Cyrillic glyphs) and
import it once:

1. Copy `YourFont.ttf` into this folder.
2. Start the editor. `FontImporter` registers `.ttf` assets, so the first scan imports the font
   and writes `YourFont.ttf.meta` (the asset guid) plus the runtime artifact
   `game/artifacts/runtime/<guid>/0` in the `FontBinary` format (SDF atlas + glyph metrics).
3. Copy the guid from the `.meta` into `kDefaultUiFontGuid` in
   `engine_source/graphics/include/engine/graphics/UiSystem.hpp`:

   ```cpp
   inline constexpr uint64_t kDefaultUiFontGuid = 1234567890123456789ull;
   ```

   `0` disables the game UI: the tree is still built, but `UiPass` draws nothing and a warning
   is logged once.
4. Rebuild the editor and the game client. The build puts the font artifact into the common pak
   because `BuildSettings.fonts` is filled from `UiSystem` in `CreateBuild`.

## Bake settings

Everything about the bake lives in the asset's `.meta` (`settings`), so the font can be
re-imported without touching code:

```json
{"header":{"guid":1234567890123456789,"importer_type":"FontImporter","sub_assets":[]},
 "settings":{"em_size":40.0,"px_range":4.0,"padding":4,"atlas_width":1024,"atlas_height":1024,
             "pixel_dist_scale":32.0,"onedge_value":128,
             "ranges":"0x20-0x7E,0xA0-0xFF,0x400-0x4FF,0x2010-0x2027,0x2030,0x2039,0x203A,0x20AC"}}
```

- `ranges` is the glyph set. A script/language needs its codepoints baked here; there is no
  runtime rasterizer.
- `em_size` and the atlas size trade memory for crispness: the atlas is R8, `1024x1024` holds
  Latin + Cyrillic + UI symbols at `em_size = 40`. If the bake reports "atlas too small", raise
  `atlas_width`/`atlas_height` or trim `ranges`.
- Bump `FontImporter::GetVersion()` when the artifact contents change, so every font is
  re-imported once.

Check a bake without the editor:

```bash
tools/agent/font-probe.sh game/assets/fonts/YourFont.ttf
```

It compiles the real `FontBaker.cpp`, prints glyph/empty/missing counts and atlas fill, and
validates the artifact with the runtime's own `FontBinary::Unpack`.

Font licensing: commit the font together with its license. `.ttf` outlines are preferred for the
SDF bake; a variable font must be instanced to a static weight first.
