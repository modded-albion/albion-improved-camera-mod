# Masters of Albion: improved camera

A mod DLL (`improved_camera.dll`) for the mod loader of Masters of Albion (the project `mod_loader`). 

- The aim is to make the hand control the camera better and to give more freedom of movement to the player.
- The camera turns about the hand, and the zoom and the turn are much smoother.
- Zooming now zooms to the hand.
- You can now pitch with the middle mouse button.
- The objects near the camera become see-through, and an object that the camera is in is hidden. Camera collisions are
  disabled because they caused problems with the new level of freedom in camera angles.

## Build

Requirements: Visual Studio with the C++ tools, and the vcpkg checkout of Masters of Albion Reforged.

```bat
git -c protocol.file.allow=always submodule update --init
tools\preset.bat x64-release
```

## Tests

`ctest --preset x64-release` runs the GoogleTest tests of the camera logic in the model of the game.

## Install (not done by the build)

The DLL goes into `<Steam game>\mods\`, next to the loader `version.dll` in the game folder:

```bat
python ..\mod_loader\tools\install.py mods add build\x64-release\improved_camera.dll
```
