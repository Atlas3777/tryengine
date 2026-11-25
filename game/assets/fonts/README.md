# UI font

The game UI can use any number of baked fonts; each is named by a key in the shared UI bundle.
Put a `.ttf` here (Latin + Cyrillic glyphs) and import it once:

1. Copy `YourFont.ttf` into this folder.
2. Start the editor. `FontImporter` registers `.ttf` assets, so the first scan imports the font
   and writes `YourFont.ttf.meta` (the asset guid) plus the runtime artifact
   `game/artifacts/runtime/<guid>/0` in the `FontBinary` format (SDF atlas + glyph metrics).
3. Put the guid from the `.meta` into the shared UI bundle, `game/data/bundles/ui.bundle`,
   under a style key:

   ```text
   {
       name: string = "font/body"
       kind: string = "font"
       ref: string = "guid:1234567890123456789"
   }
   ```

   A style names the key (`UiStyle(font = "font/body")`); the UI resolves it through the
   screen's `AssetMap` and `UiSystem` builds the per-font material. Add another font as another
   key and use it in the style that needs it. A screen bundle can override the same key with its
   `parent = "data/bundles/ui.bundle"` hierarchy. Without a resolvable key the text is not
   drawn and `UiSystem` logs why.
4. Rebuild the editor and the game client. The build packs the font into the common pak because
   it reads `assets` of every bundle (`kind = "font"`); there is no separate build setting.

## Bake settings

Everything about the bake lives in the asset's `.meta` (`settings`), so the font can be
re-imported without touching code:

```text
header: AssetMetaHeader = {
	guid: u64 = 1234567890123456789
	importer_type: string = "FontImporter"
	sub_assets: u64[] = [
	]
}
settings: FontBakeSettings = {
	em_size: f32 = 40
	px_range: f32 = 4
	padding: u32 = 4
	atlas_width: u32 = 1024
	atlas_height: u32 = 1024
	pixel_dist_scale: f32 = 32
	onedge_value: u8 = 128
	ranges: string = "0x20-0x7E,0xA0-0xFF,0x400-0x4FF,0x2010-0x2027,0x2030,0x2039,0x203A,0x20AC"
}
```

Select the `.ttf` in the Asset Browser to edit these fields in the **Import Settings** panel and
press **Save & Reimport**; the panel does not show raw text.

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
