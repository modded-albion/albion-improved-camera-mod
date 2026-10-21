# Masters of Albion: improved camera

A mod DLL (`improved_camera.dll`) for the mod loader of Masters of Albion (the project `mod_loader`). It is the
hand and camera rework of the research mod of Masters of Albion Reforged (`tools/research_mod`, the `camera` parts),
moved into its own DLL. That repository stays as it is.

What it does, when `camera.enabled` is true in mods.json:

- The camera turns about the hand, and the zoom and the turn are smooth (with glides after a drag, a turn or a pitch).
- The hand rig: the pitch with the middle mouse button (modes `hand`, `inPlace`, `orbit`), the look up near the land,
  the spring arm that keeps the camera in front of the land and the objects, the land grip, the chase of the camera,
  the soft edge of the map, the fly-to on a double click, and the ground look below the closest zoom stage.
- The objects near the camera become see-through, and an object that the camera is in is hidden.
- Four camera spots for each save (`%LOCALAPPDATA%\MastersOfAlbionReforged\camera-spots.json`) and the reset of the
  view with the north at the top.
- A trace of the camera (CSV) and a black box of the last frames, in the log folder of the loader or in
  `camera.traceFolder` (never in a game folder).

## How it uses the loader

The DLL exports `MoaModApiVersion`, `MoaModGetInfo` (id `improved_camera`), `MoaModInit` and `MoaModShutdown`
(`src/game/ModMain.cpp`). In `MoaModInit` it:

- finds the IL2CPP exports and the managed names that it needs itself (common `moa_mod_game`). A missing name stops
  the init with 0, so the loader unloads the mod and the game continues;
- registers the settings section `camera` of mods.json, the fields of the menu Camera (`CameraFields`), the actions
  with their hotkeys and buttons (`toggleCamera` Ctrl+Shift+F5, `cyclePitchMode` Ctrl+Shift+F7, `toggleCursorLock`
  Ctrl+Shift+F8, `toggleTrace` Ctrl+Shift+F9, `saveSpot1..4` Ctrl+F1..F4, `goToSpot1..4` F1..F4, `resetView` F5),
  a readout and a status reader;
- asks the loader for callbacks before and after `GameManager.Update`, and before
  `EffectCameraShake.OnBeginCameraRendering` (the render sample of the trace). The loader installs the hooks.

`MoaModShutdown` gives the objects of the fade back to the game, removes the render sample and closes the trace.

When the loader has inspector queries, the mod also adds two queries (the inspector mod calls them; the loader runs
them on the main thread). They are the camera queries of the research mod, with the same answers:

- `camera.state`: the camera of the game and the state of the camera mod;
- `camera.events`: the last events of the camera mod (parameter `count`, 50 if not given).

The queries `autotest.*` of the research mod (the camera tests with the virtual input in the game) are not in this
DLL, because the virtual input is only in the tests.

Changes from the research mod:

- The reset of the view uses `camera.northDegrees` (new, default 0). The research mod used `moon.northDegrees`, which
  now belongs to the sky mod. Set both to the same value.
- The hook on `EffectCameraShake.OnBeginCameraRendering` is always there (the registrations are only possible in the
  init). Its callback does nothing while the trace is off. The research mod installed it only for a trace.

## Build

Requirements: Visual Studio with the C++ tools, and the vcpkg checkout of Masters of Albion Reforged.

```bat
git -c protocol.file.allow=always submodule update --init
tools\preset.bat x64-release
```

The result is `build\x64-release\improved_camera.dll`. It imports only `KERNEL32.dll` and `USER32.dll` (static C
runtime). Dependencies: vcpkg in manifest mode (`vcpkg.json`: nlohmann-json, spdlog, GoogleTest) with the triplet
`x64-windows-static`. The presets use `VCPKG_ROOT` = `D:/masters_of_albion/masters_of_albion_reforged/vcpkg` (the
vcpkg checkout of the reforged repository; this repository has no vcpkg submodule), the binary cache
`D:/masters_of_albion/cache/vcpkg-archives` and the downloads `D:/masters_of_albion/cache/vcpkg-downloads`.

## Folders and include lines

Each library has one folder: `src/core` (`moa_camera_core`), `src/game` (`improved_camera.dll`) and `tests/support`
(`moa_camera_autotest`). The targets put `src` (or `tests`) on the include path, and common puts
`external/common/src` there. An include line thus names the library folder and the file, for example
`#include <core/CameraLoop.h>`, `#include "game/CameraMod.h"` or `#include <support/Scenario.h>`. The folders of
common have other names (for example `mod_core`, `mod_game`), so an include path never finds two files. The C++
namespaces do not follow the folders.

## The link to common

`external/common` is a git submodule with the URL `D:/masters_of_albion/projects/common` (an absolute local path).
Git 2.43 refuses local file submodules by default, so each submodule command needs `-c protocol.file.allow=always`.
`CMakeLists.txt` includes it with `add_subdirectory(external/common)`.

## Tests

`ctest --preset x64-release` runs the GoogleTest tests of the camera logic in the model of the game (the camera loop,
the hand rig, the pitch, the spring arm, the land, the ground look, the navigation, the fade, the trace), the
settings and fields of the camera, and the scenarios of the camera controls (`tests/autotest/scenarios`) with the
scenario runner and the virtual input. The scenario runner and the virtual input are only in the tests
(`tests/support`), not in the DLL. `tests/data` has small extracts of camera traces of the research mod: our own
output, not game data.

## Install (not done by the build)

The DLL goes into `<Steam game>\mods\`, next to the loader `version.dll` in the game folder. Only the user starts the
install, with the install tool of `mod_loader`:

```bat
python ..\mod_loader\tools\install.py mods add build\x64-release\improved_camera.dll
```
