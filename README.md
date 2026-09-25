# PinOut Lab

Unofficial research tools for **PinOut 1.0.7 / 1000700**: an Android developer
overlay, native body editing, ball and camera controls, table-loading
instruments, and a desktop editor connected to the running Lab through ADB.

This is an experimental source release. Supply your own compatible APK.
Game binaries and assets are not included.

This is a fan project. It is not affiliated with, endorsed by or connected to Mediocre AB, who publish PinOut. The game's name is used only to say which game the research is about, and it remains their trademark. See [LICENSING.md](LICENSING.md).

## Build and install

The native build scripts target Linux x86_64. Use Python 3.11.8+ and place the
original APK at `incoming/pinout/pinout.apk`. Its SHA-256 must be:

```text
81c0f9048c2c12731fefcf0373f0fccb28a2e0626288bc826ba66f5002f37ab7
```

Run from the repository root:

```bash
python3 -m venv tools/venv
tools/venv/bin/python -m pip install -r tools/requirements.txt
tools/venv/bin/python tools/bootstrap.py --components build
source tools/env.sh
python tools/build_game_lab.py pinout
adb install -r artifacts/pinout-lab.apk
adb shell am start -n com.mediocre.pinout.dev/com.mediocre.pinout.MainActivity
```

The bootstrap downloads SDK, NDK, JDK and APK tooling, requiring several GB.
The Lab installs as `com.mediocre.pinout.dev`, alongside the original.
Build output and the local signing key are under `artifacts/` and `build/`.
Keep that key to install compatible updates.

## Desktop editor

Start a run in the installed Lab and connect an ADB-authorized device. With
the environment above active:

```bash
python -m pip install -r desktop/requirements.txt
python tools/setup_desktop.py
python -m desktop.game_server --game pinout --port 8767
```

Open http://127.0.0.1:8767 and choose **Read scene from game · pause**. Use
`--serial DEVICE_SERIAL` if multiple devices are connected. This editor reads
the native scene from the device; it does not open the APK as an offline level.
See [desktop controls](docs/native_scene_editor.md).

## Scope and verification

- The addon targets x86_64 and ARM64, with minimum Android API 21.
- Historical reports describe x86_64 emulator tests on API 26/30. ARM64
  device behavior has not been verified in those reports.
- Body edits rebuild collision triangles and render buffers; they do not
  regenerate joint anchors, mass/inertia or baked lighting.
- Saved edits are overrides, not a complete source-level table editor.

Start with [PinOut research](docs/pinout.md),
[developer controls](docs/developer_mode.md), and the
[historical experiment report](docs/game_lab_experiments.md).
[The documentation index](docs/README.md) identifies the shared Smash Hit reference
pages. The [notebook](REVERSE_ENGINEERING.md) preserves the three-game research
chronology; recorded results are not fresh test results for this checkout.

Run `python3 tools/check_source.py` for the source-only checks used by CI.
See [verification scope](docs/VERIFICATION.md) and [contributing](CONTRIBUTING.md).

## License

Original tooling and notes use the [MIT license](LICENSE).
[Third-party notices](dev/THIRD_PARTY.md) apply to dependencies. No license to
PinOut or its assets is granted; this project is unofficial.

Written by Luca Zani ([OverloadedTech](https://github.com/OverloadedTech)).
This repository is a standalone source export of the PinOut tooling from a
larger private research workspace covering three games, so its history starts
at the export rather than at the first day of the work. That workspace holds
the original APKs, decompiled game code, extracted assets and device captures,
none of which can be redistributed, so publishing its history was never an
option. The tools and the notebook are the part that can be shared.
