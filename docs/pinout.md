# PinOut research

Supplied build: **1.0.7 / 1000700**, package `com.mediocre.pinout`.
APK SHA-256: `81c0f9048c2c12731fefcf0373f0fccb28a2e0626288bc826ba66f5002f37ab7`.
The research now includes an embedded Android developer addon and a live
desktop geometry editor. See the [controls](other_game_devkits.md) and the
[desktop workflow](native_scene_editor.md). The historical original-game
streaming probe and embedded addon have separate validation reports.

## Runtime and engine

**CONFIRMED:** Native C++ Qi framework, Lua 5.2.0, EGL/OpenGL ES and OpenSL ES.
The Android `MainActivity` extends GameActivity and loads `libpinout.so`.
The APK includes ARM64, ARMv7, x86 and x86_64, two DEX files and Crashlytics
libraries. Managed code handles Android/platform integration; native `Game`,
`Level`, `Table`, `Body`, `Mesh`, `Physics`, `Camera` and rendering classes
implement gameplay. This shares framework technology with Smash Hit, not
identical object layouts. See the [comparison](mediocre_comparison.md).

The observed x86_64 build ID is
`eff8efba16b53a2a60f5abae5f485ee8a3edf17e`. Native function and field mappings
in the experimental probe apply only to that build. Original menu, gameplay,
restart and physics resumption ran on the API 30 x86_64 emulator.

**CONFIRMED, static:** `Physics::update` normally calls its simulation routine
ten times with `Game.dt / 10 * 0.85`; capture mode uses one substep instead.
`Physics::simulate` inserts native bodies, generates convex/ball/floor contacts
and calls `tdSolverStep`. Td is actively used here. Granny Smith's main physics
path instead calls Box2D. A shared collision/solver family does not imply the
games have the same collision shapes or world axes.

## Table representation

**CONFIRMED:** `assets/game.xml.mp3` contains nine `level` groups with
12, 15, 15, 18, 9, 13, 13, 27 and 3 table entries. That is **125 ordered entries
referencing 122 distinct table paths**. The native array in the measured run
also had 125 entries. The APK contains 167 table XML documents, including
additional authoring/test material; these are not all campaign entries.

Each table XML has properties such as `size` and `template`, plus typed
entities. Body transforms are authored with two position coordinates and a
rotation. Curved mesh records carry control points and attributes such as
height, ramp opening, width, friction, restitution and fences. Decals, drops,
prefabs and scripted objects have distinct representations. A cached mesh is
not the entire editable entity model.

The pinball table lies in X/Y, with Z as height. Its longitudinal
progress variable is the ball's Y coordinate. These are native units;
they have not been calibrated to real-world meters. Entering a table rebases
tables, camera, ball and other spatial data, accumulating an origin offset.
The probe records both local position and accumulated origin so a rebase is
not mistaken for teleportation.

## Cached geometry and light maps

[pinout_formats.py](../tools/pinout_formats.py) validates all 138 DAT files
and 138 LGT files, consuming each decompressed stream exactly. Together
the geometry files contain 5,863 mesh records, 1,075,103 render vertices,
1,145,597 render triangles, 848,197 collision vertices and 776,400 collision
triangles. These totals include non-campaign assets, repeated source geometry
and the older cache variant; they are not the simultaneous runtime draw count.

**CONFIRMED, native reader plus full-payload validation:** 132 DAT files use
the current quantized format. A stream concatenates mesh records. Each has:

1. A 32-bit render-vertex count, then packed 21-byte vertices: three signed
   16-bit positions, three signed 8-bit normals, two float UVs and a packed
   32-bit color. Position decoding is `component * 16 / 32767`; normal decoding
   is `component / 127`.
2. A 32-bit render-triangle count, then a one-byte boolean and three unsigned
   16-bit indices per triangle. The boolean selects an index-buffer path.
3. A 32-bit collision-vertex count, then three signed 16-bit positions and
   three signed 8-bit normals per vertex.
4. A 32-bit collision-face count, then three unsigned 16-bit indices and one
   byte of flags. The reader separates the low four flag bits.

**CONFIRMED, payload validation; current runtime use UNKNOWN:** Six other DAT
files store float positions instead, giving 27-byte render vertices. Their
collision vertices contain six floats; the triangle layouts remain as above.
All six parse to EOF with valid indices and normals. None of these six paths
is referenced by the inspected `game.xml`. They initially failed the current
schema decoder; that failure and the separate schema experiment are retained.

The current reader can omit records for a retained/generated mesh, so the
decoder does not assign object IDs merely from record order. Mesh-local
coordinates also require body transforms and table offsets before becoming
world geometry.

**CONFIRMED:** Each LGT is a zlib-compressed **128 × 256, one-byte GL_ALPHA**
texture: 32,768 decoded bytes. `Table::loadLightMap` uses those exact dimensions
and format. It is not a guessed 16-bit square image.

## Streaming algorithm

**CONFIRMED, static and controlled x86_64 experiments:** `Level::tick` selects
the current table from strict longitudinal intervals:

`table.offsetY < ball.y < table.offsetY + table.length`.

The inspected lookup falls back to the last table if no interval matches.
Exact seam coordinates and arbitrary positions outside every interval have
not been separately exercised; the deliberate jumps used table interiors.

For current index `k`, set `left = max(1, k) - 1` and
`right = min(k + 2, tableCount - 1)`. Normal streaming does the following:

1. Activate `left..right`; deactivate every previously active table outside it.
2. Incrementally preload `k - 2` when `k > 1`, and `right + 1` when valid.
3. Deload `k - 3` when valid, and `right + 2` when valid, provided those tables
   are inactive and have started loading.

At the beginning this activates 0–2 and preloads 3. Away from the endpoints,
the active window is **k−1, k, k+1, k+2**. Preloading and deloading have separate
boundaries. There are additional rewind/mode branches; the measured algorithm
is the regular-run branch.

`Table::preload` advances a stage counter. Early stages create authored bodies,
read and decompress geometry; stages 10–90 process meshes; later stages upload
buffers and load lighting. Completion is **stage 100**. `isPreloading()`
returns true for any positive stage, including 100, so its name alone is not
an exclusive debug state. Activation loops synchronously until stage 100 if
the table is not already ready. Incremental work runs through native ticks;
the experiment does not establish a separate asynchronous I/O worker.

**Confirmed distinction:** Every loaded table retains a generated
base body at `Table+0x340`. `unloadBodies` explicitly excludes that body.
Deloading frees authored bodies, non-retained mesh geometry, shared vertex/index
buffers, light-map resources and temporary buffers, while preserving the Table
object, its base body and other metadata/non-body entities. A count of one
remaining body does not mean the whole table is resident.

Deactivation only clears active behavior/script state. Because deloading
targets particular neighboring indices, a large jump can leave old cached
tables far outside the new window. The cache therefore depends on traversal
history. It is not accurately described as “only four tables exist.”

## Experiments and evidence

The original menu-to-game observation recorded 194 samples, 125 table loads,
three initial activations and four completed preloads over 241 wall-clock
seconds. The executable remained original; the read-only probe observed
exported lifecycle calls. Evidence: `analysis/games/pinout/runtime/menu-to-game/`.

The completed controlled study passed **16 checks**:

| Experiment | Observed result |
| --- | --- |
| Hold ball, continue native ticks | Position and settled residency stay fixed |
| Move camera +20 X, +100 Y, +20 Z | Actual render camera moves; ball and residency stay fixed |
| Turn view 180° around world up | Rotation changes; no table loading |
| Move ball sideways, up or down | No longitudinal table change |
| Jump ball to table 10 interior | Activates 9–12 immediately; old 0–3 caches remain inactive but resident |
| Inspect intervening tables 4–7 | Their authored bodies are not loaded by the jump; their base bodies remain |
| Jump backward to table 1 | Activates 0–3 again |
| Advance through tables 2, 3 and 4 | Neighbor deload boundaries run; table 0's authored bodies disappear while its metadata/base remain |
| Restore pose and release controls | Original physics executes again |

The study held `Physics::update` and explicitly set the ball pose before native
ticks; it overrode the camera after its normal update. Original table
selection, activation, preload and deload logic remained running. The
intervention is a research instrument, not a packaged free-camera product.

Passing evidence: `analysis/games/pinout/runtime/streaming-fourth/`, PID 31638,
348.2 wall-clock seconds, 10,712,121 bytes of bounded events. The native
simulation clock and software-emulator wall time are not interchangeable.
Earlier attempts remain archived: an expired run, an original tutorial pause,
and an assertion that incorrectly assumed deloaded tables had zero bodies.
The latter led to the retained-base-body discovery. None is relabeled a pass.

Reproduce using the exact original build and matching Frida 17.17.0 server:

```sh
source tools/env.sh
python tools/pinout_formats.py
python -m pip install frida==17.17.0
# Start the matching x86_64 Frida server on the rooted research device.
# Forward its listening port: adb -s emulator-5554 forward tcp:27042 tcp:27042
python experiments/observe_pinout.py --out analysis/my-pinout-observation
# Start a fresh original run and dismiss the original tutorial cards first.
python experiments/verify_pinout_streaming.py --out analysis/my-pinout-study
```

## Rendering and remaining work

**CONFIRMED, shader assets:** The 3D shader supports light maps, reflections,
per-pixel lighting/specular, fog and view-dependent bending. The composite
shader combines sharp and blurred images with a vertical blur weight, then
adds bloom. The presence of a shader variant alone does not prove every option
is active in every scene.

## Embedded addon evidence

**CONFIRMED:** The embedded x86_64 addon passed 34 native integration checks
in `analysis/games/pinout/runtime/embedded-validation-third/`. These include
six-axis camera movement, looking behind, FOV commands, real render-triangle
selection, native body/group transforms and undo, saved overrides after reload
and process death, ball noclip and movement, unlimited time, forward/backward
activation, direct access to the last table and original simulation resumption.

Edits are checked with the original `Physics::raycast`. A real body is moved
outside the corridor to isolate it; native rays then hit its translated and
scaled collision faces. Shared render-buffer bytes change as well. The table's
implicit floor is a separate native plane, not an editable mesh. Visible bodies
without colliders can be selected through their actual render triangles.

Validation found a paused-restart defect: native reset clears table resources,
while the Lab pause suppresses the next loading tick. The addon now primes
that original tick with zero elapsed simulation time. Saved-edit reloads and
table navigation pass without releasing the pause. Active tables finish
loading synchronously to stage 100.

The first collision fixture selected no candidate faces because it assumed a
centroid above 0.025 units on a body only 0.0347 units tall; no native rays ran
in that attempt. The corrected fixture uses actual vertical side faces above
the implicit floor. This test-fixture error is separate from the real restart
defect. Both failed attempts remain recorded.

**UNKNOWN / unfinished:** Full shader/material configuration, every mode and
rewind branch, duplicate-XML-attribute behavior, original save formats, source
XML/cache rebaking, mass/inertia and joint-anchor regeneration. Android touch,
desktop and final APK checks are recorded separately from the native suite.
Physical ARM64 testing remains outstanding. Smash Hit offsets do not apply.

**CONFIRMED (current APK):** The final `runtime/native-release/`,
`touch-release/` and `desktop-release/` reports pass 34, 11 and 12 checks.
Android gestures move real native geometry and update its baked vertex bytes;
visible buttons undo and save those changes. Actual desktop handle dragging,
group saving and stale-scene rejection also pass. See
[release verification](game_lab_experiments.md) for the exact APK/source hashes
and the API 26 emulator's documented SystemUI workaround.

The original full Android lists caused excessive layout/GC work in an ANR
trace. Object and table lists now use search and twelve-entry pages. Live
status parses a compact cached snapshot; complete objects, tables, replies and
research data remain available through explicit inspection and the socket.
The panel list is a captured snapshot; Refresh list updates it.
