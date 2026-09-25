# PinOut developer mode

Use the [build and install instructions](../README.md). The addon builds to
`artifacts/pinout-lab.apk`; its report is `artifacts/pinout-build-report.json`.
It installs as `com.mediocre.pinout.dev` and launches the original
`com.mediocre.pinout.MainActivity` with the Lab overlay. Supported ABIs are
x86_64 and ARM64.

The [developer-kit guide](other_game_devkits.md) describes Play, Edit, Camera,
ball and pause controls. [PinOut research](pinout.md) documents native table
loading, physics and editing limits.

Commands and saved overrides use the shared `labs/common/` addon and the
`labs/pinout/` adapter. Its socket is `pinout_lab`. Files live under the Lab
app's `files/mediocre-lab/` directory, including `level-edits.json`,
`preferences.json`, `scene.json` and rotating event logs.

The [native scene editor](native_scene_editor.md) uses `desktop.game_server`
and reads geometry from a running Lab. Historical test setup and results are
in [game Lab experiments](game_lab_experiments.md); current source-check scope
is in [verification](VERIFICATION.md).
