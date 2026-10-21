# ******************************************************************************
# Copyright (c) 2026 Masters of Albion Reforged developers
# ******************************************************************************

# The improved camera mod of Masters of Albion: a mod DLL for the mod loader. The camera turns about the hand, the zoom
# and the turn are smooth, and the hand rig adds the pitch, the spring arm, the ground look, the camera spots and the
# fade of the objects near the camera. The DLL uses only the static C runtime and static libraries.

cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

project(
  moa_improved_camera
  VERSION 0.1.0
  LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(MOA_CAMERA_BUILD_TESTS "Build the tests of the camera mod" ON)

# The shared code (git submodule).
add_subdirectory(external/common)

# The camera logic that does not need the game: the camera loop with its model of the game, the hand rig, the pitch,
# the spring arm, the ground look, the navigation, the fade rules, the trace and the settings.
add_library(
  moa_camera_core STATIC
  src/core/CameraBlackBox.h
  src/core/CameraChase.h
  src/core/CameraDrag.h
  src/core/CameraEvents.h
  src/core/CameraFade.h
  src/core/CameraFeel.h
  src/core/CameraGround.h
  src/core/CameraInertia.h
  src/core/CameraLand.h
  src/core/CameraLoop.h
  src/core/CameraNav.h
  src/core/CameraPitch.h
  src/core/CameraPivot.h
  src/core/CameraSettings.h
  src/core/CameraTrace.h
  src/core/HandRig.h
  src/core/MouseButtons.h
  src/core/CameraBlackBox.cpp
  src/core/CameraEvents.cpp
  src/core/CameraFade.cpp
  src/core/CameraLoop.cpp
  src/core/CameraLoopChase.cpp
  src/core/CameraLoopGround.cpp
  src/core/CameraLoopHand.cpp
  src/core/CameraLoopNav.cpp
  src/core/CameraNav.cpp
  src/core/CameraSettings.cpp
  src/core/CameraTrace.cpp
)
target_include_directories(moa_camera_core PUBLIC src)
target_link_libraries(moa_camera_core PUBLIC moa_common moa_mod_core)
moa_set_warnings(moa_camera_core)

if (WIN32)
  add_library(
    improved_camera SHARED
    src/game/CameraMod.cpp
    src/game/CameraMod.h
    src/game/GameFade.cpp
    src/game/GameFade.h
    src/game/ModMain.cpp
  )
  target_include_directories(improved_camera PRIVATE src)
  target_link_libraries(improved_camera PRIVATE moa_camera_core moa_mod_game moa_mod_sdk)
  moa_set_warnings(improved_camera)
endif ()

if (MOA_CAMERA_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif ()
