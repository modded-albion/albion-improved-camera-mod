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
  src/core/include/moa_mod/camera/CameraBlackBox.h
  src/core/include/moa_mod/camera/CameraChase.h
  src/core/include/moa_mod/camera/CameraDrag.h
  src/core/include/moa_mod/camera/CameraEvents.h
  src/core/include/moa_mod/camera/CameraFade.h
  src/core/include/moa_mod/camera/CameraFeel.h
  src/core/include/moa_mod/camera/CameraGround.h
  src/core/include/moa_mod/camera/CameraInertia.h
  src/core/include/moa_mod/camera/CameraLand.h
  src/core/include/moa_mod/camera/CameraLoop.h
  src/core/include/moa_mod/camera/CameraNav.h
  src/core/include/moa_mod/camera/CameraPitch.h
  src/core/include/moa_mod/camera/CameraPivot.h
  src/core/include/moa_mod/camera/CameraSettings.h
  src/core/include/moa_mod/camera/CameraTrace.h
  src/core/include/moa_mod/camera/HandRig.h
  src/core/include/moa_mod/camera/MouseButtons.h
  src/core/src/camera/CameraBlackBox.cpp
  src/core/src/camera/CameraEvents.cpp
  src/core/src/camera/CameraFade.cpp
  src/core/src/camera/CameraLoop.cpp
  src/core/src/camera/CameraLoopChase.cpp
  src/core/src/camera/CameraLoopGround.cpp
  src/core/src/camera/CameraLoopHand.cpp
  src/core/src/camera/CameraLoopNav.cpp
  src/core/src/camera/CameraNav.cpp
  src/core/src/camera/CameraSettings.cpp
  src/core/src/camera/CameraTrace.cpp
)
target_include_directories(moa_camera_core PUBLIC src/core/include)
target_link_libraries(moa_camera_core PUBLIC moa_common moa_mod_core)
moa_set_warnings(moa_camera_core)

if (WIN32)
  add_library(
    improved_camera SHARED
    src/game/moa_mod/camera/CameraMod.cpp
    src/game/moa_mod/camera/CameraMod.h
    src/game/moa_mod/camera/GameFade.cpp
    src/game/moa_mod/camera/GameFade.h
    src/game/moa_mod/camera/ModMain.cpp
  )
  target_include_directories(improved_camera PRIVATE src/game)
  target_link_libraries(improved_camera PRIVATE moa_camera_core moa_mod_game moa_mod_sdk)
  moa_set_warnings(improved_camera)
endif ()

if (MOA_CAMERA_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif ()
