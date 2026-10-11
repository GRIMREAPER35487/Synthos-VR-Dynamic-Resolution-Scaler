# Synthos VR Dynamic Resolution Scaler

A small Windows tool for SteamVR that adjusts each game's render resolution on the fly to hold your headset's frame rate. When GPU frame time runs over budget it lowers the per-game resolution; when there's headroom it raises it again.

It runs as a SteamVR overlay app (it can start automatically with SteamVR) and changes the same per-application resolution setting you'd otherwise set by hand in SteamVR's settings.

## Features

- **Per-game dynamic resolution** using SteamVR's per-app resolution setting. Each game resumes from the scale it last used.
- **Automatic refresh-rate detection** from the headset, or a manual target (including half-rate targets for SSW / motion smoothing).
- **Smoothed, stable adjustments**: time-based smoothing, a dead band around the target, 2% steps and a cooldown between changes, with a faster reaction to severe GPU overruns.
- **CPU bottleneck shield**: detects CPU-bound frames and fits the GPU work into the remaining budget, down to a configurable emergency floor.
- **Live telemetry**: current scale and render size, GPU/CPU frame times, and SteamVR's own late-frame statistics (and whether the CPU or GPU caused them).
- **"Force 50%" test** to check that resolution changes take effect in a game.

## Install

1. Close SteamVR.
2. Run `Synthos_VR_Dynamic_Resolution_Scaler_Setup_<version>.exe`.
3. Start SteamVR. The app is registered as a startup overlay app; you can toggle it under SteamVR's *Startup Overlay Apps* settings.

If the app doesn't show up in SteamVR, open it and click **Register with SteamVR**, then restart SteamVR.

## Usage

Open the app from the Start menu (or let SteamVR start it). With SteamVR closed it works as a settings editor; once SteamVR is running it connects automatically and starts managing whichever game has focus.

| Setting | What it does |
|---|---|
| Master Enable | Turns dynamic resolution on or off. |
| Auto-Detect HMD Refresh Rate | Uses the headset's refresh rate as the target; turn off to pick one manually. |
| SSW / Frame Gen Half-Rate Target | Targets half the refresh rate (e.g. 60 fps on a 120 Hz headset). |
| Auto-Detect CPU Bottlenecks | Enables the CPU bottleneck shield. |
| Adjustment Delay / Lock Cooldown | Minimum time between resolution changes. |
| CPU Emergency Scale Hard Floor | Lowest scale the CPU shield may use. |
| Minimum / Maximum Resolution Scale | Range the scaler stays within. |
| Enable Debug File Logging | Writes `debug.log` next to the settings file. |

Settings and the debug log are stored in `%APPDATA%\Synthos VR Dynamic Resolution Scaler\`.

## Building from source

Requirements: Windows, Visual Studio 2022 (or Build Tools) with the C++ workload, CMake 3.20+, Git. Dependencies (GLFW, Dear ImGui, OpenVR SDK) are downloaded automatically by CMake.

```bash
cmake -S . -B build
cmake --build build --config Release
```

The app is built to `build/Release/Release/Syn_Dynamic_Res_Scale.exe`.

To build the installer, install [Inno Setup 6](https://jrsoftware.org/isinfo.php), build the app first (the installer takes its version from the CMake build), then:

```bash
iscc installer/setup.iss
```

The installer is written to `installer_output/`.

### Version number

The version is set in one place: `project(... VERSION x.y.z ...)` in `CMakeLists.txt`. CMake generates it into the app and the installer.

### Tests

```bash
ctest --test-dir build -C Release
python installer/tests/run_appconfig_tests.py
```

The first runs the C++ unit tests (smoothing engine and frame timing); the second checks the installer's SteamVR registration logic against sample files (requires Inno Setup).

GitHub Actions builds the app, runs both test suites and produces the installer on every push; the installer is attached to each run as a downloadable artifact.

## License

MIT. See [LICENSE](LICENSE).
