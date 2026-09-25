# Level and asset representation

> Shared Smash Hit reference. PinOut has different formats; see [game research](pinout.md).

**CONFIRMED authoring hierarchy:** `game.xml` selects named levels → `levels/*.xml` lists room definitions → `rooms/*.lua` constructs segments and obstacles → `segments/*.xml` and `.mesh` define environment geometry → `obstacles/*.lua` construct scripted native entities.

Level XML contains ordered `<room type="basic/basic" length="300" start="true" end="true"/>` records. A room's physical length is computed by its Lua script with `mgLength`; the XML `length` also participates in reported progression distance. Do not equate displayed distance with physical world units without the room-specific conversion.

Room scripts accumulate `l = l + mgSegment(name, -l)` and use negative Z offsets. `rooms/include.lua` implements weighted segment selection, avoids immediate repeats, and limits configured counts. `LevelScript::load` seeds its `QiRandom` with zero in mode 0/capture mode and `rand()` otherwise. Mode names are defined in the shipped menu Lua: training=0, classic=1, expert=2, zen=3, versus=4, cooperative=5. Repeated classic-mode checkpoint-zero rebuilds produced different native segment sequences; a fixed checkpoint does not imply a fixed layout. Lua also exposes standard random functions, and scripts must be checked individually.

Segment XML's root declares dimensions and environment properties. Child nodes contain:

- `box`: position, half-extents (`size`), visibility/reflection, colors, tile and texture mapping attributes. Native loading always creates the corresponding static collision shape, even when a rendering attribute differs.
- `obstacle`: Lua type, position/rotation, and named parameters (`param0`, etc.). These become deferred native `ObstacleDef` records.
- `decal`, `powerup`, `water`, and `model`: their respective game-specific data.

Default attributes can come from templates. The report `analysis/reports/segments.json` preserves explicit source attributes; that report does not invent omitted values.

**CONFIRMED segment mesh binary layout**, validated against all 643 files:

1. Inflate the entire file as a zlib stream.
2. Little-endian `uint32 vertex_count`.
3. `vertex_count` records of 24 bytes: position X/Y/Z (`float32`), UV U/V (`float32`), RGBA (four bytes).
4. Little-endian `uint32 triangle_count`.
5. `triangle_count` records of three little-endian `uint32` vertex indices.

There are no per-box IDs in this baked mesh representation. Native `RenderBatch::load` reads it, adds the segment's Z offset, and creates a VBO plus triangle and wireframe index buffers. XML collision boxes and the baked visible mesh are distinct representations; changing one does not automatically change the other.

**CONFIRMED MTX wrapper:** three little-endian words `(kind, payload_bytes, reserved_zero)`, followed by payload. Kind 0 is JPEG. Kind 1 payload begins `(version=1, width, height, jpeg_bytes)`, then JPEG RGB, then `compressed_alpha_bytes` and zlib-compressed width×height alpha bytes. This is established by `QiMtxDecoder` and full decoding of all 522 files: 15 JPEG payloads and 507 JPEG-plus-alpha payloads, totaling 73,907,814 pixels. Every JPEG decodes, every declared dimension agrees, and all alpha streams have the exact expected size and end without trailing compressed data. `tools/analyze_assets.py` reproduces this validation; `analysis/reports/textures.json` contains per-file results.

Convex meshes in `assets/meshes` contain `<convex><v>x y z</v>…</convex>` points. Native code constructs convex hulls from these; obstacle geometry can also be boxes and cylinders. The separate OBJ models use the model-provider subsystem.

## Fonts and text resources

**CONFIRMED:** all nine `.fnt` and eighteen `.ufnt` files are text. The legacy `Font` loader reads per-character integer widths and normalizes them for its atlas grid. `FontUnicode::LoadFontData` reads two floating-point header values, a glyph count N, N pairs mapping Unicode code points to glyph indices, and N seven-value glyph records. Each record contains four atlas coordinates followed by three floating-point metrics (offsets and advance, as supported by the rendering consumers). All 18 Unicode files validate structurally, with 1,562 glyph records total; see `analysis/reports/fonts.json` and `analysis/decompiled/fonts/`. Exact semantic names of the two header values have not been recovered, so the inventory preserves them as raw header values. Localization also uses plain text `.txt` files and localized font atlases.

## Audio, configuration and persistence

**CONFIRMED:** all 166 `.ogg` assets begin with a Vorbis identification packet, version 0, sample rate 44,100 Hz: 97 mono and 69 stereo files. This identifies their encoding; the inventory does not claim a full decode of every audio sample. Native sound banks decode effect samples and `QiMusicStream` maintains streaming PCM buffers for music. `getLocation` converts reported played PCM byte counts using frequency, channel count and 16-bit sample width, giving seconds. The level-facing audio accessor subtracts 0.15 seconds.

`game.xml`, templates, material definitions, level lists and most gameplay/UI configuration are text XML; behavior lives in Lua. `Game::loadConfig` / `saveConfig` read/write `user://config.xml` with property bags, including an audio subsection. Android preferences and SDK stores are separate managed persistence.

**CONFIRMED static serialization:** `Player::save(QiOutputStream&)` builds XML rooted at `<smashhit>`, with version/platform metadata, profile properties, per-mode statistics and checkpoint records. `Player::save(bool)` transforms each output byte by adding a repeating embedded-key byte and the file-length low byte modulo 256 before writing `user://progression.xml`. `Player::load()` reverses that transform and also has a plain-data fallback. This is byte obfuscation around XML, not a serialized general-purpose scene graph. The cloud-save branch additionally invokes `QiCompress` before the Android platform callback. These paths were inspected without modifying profile/entitlement data. Evidence: `analysis/decompiled/save/` and `Game::{loadConfig,saveConfig}`.
