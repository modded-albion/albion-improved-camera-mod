find_package(GTest CONFIG REQUIRED)

# The tests of the camera controls run the scenario files in the model of the game. The scenario runner and the
# virtual input are only for these tests: the mod DLL does not have them.
add_library(
  moa_camera_autotest STATIC
  support/moa_mod/autotest/Scenario.h
  support/moa_mod/autotest/ScenarioRunner.h
  support/moa_mod/autotest/VirtualInput.h
  support/moa_mod/autotest/Scenario.cpp
  support/moa_mod/autotest/ScenarioRunner.cpp
  support/moa_mod/autotest/VirtualInput.cpp
)
target_include_directories(moa_camera_autotest PUBLIC support)
target_link_libraries(moa_camera_autotest PUBLIC moa_camera_core)
moa_set_warnings(moa_camera_autotest)

add_executable(
  moa_camera_tests
  AutotestHarness.h
  CameraLoopHarness.h
  FadeHarness.h
  test_CameraAutotest.cpp
  test_CameraButtons.cpp
  test_CameraChase.cpp
  test_CameraEvents.cpp
  test_CameraFade.cpp
  test_CameraFeel.cpp
  test_CameraGround.cpp
  test_CameraHandRig.cpp
  test_CameraLand.cpp
  test_CameraLoop.cpp
  test_CameraMaths.cpp
  test_CameraNav.cpp
  test_CameraSettings.cpp
  test_CameraSpringArm.cpp
  test_CameraTrace.cpp
)
target_link_libraries(moa_camera_tests PRIVATE moa_camera_core moa_camera_autotest GTest::gtest_main)
# The replay tests read small extracts of camera traces of the research mod: our own output, not game data.
target_compile_definitions(moa_camera_tests PRIVATE MOA_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
                                                    MOA_AUTOTEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/autotest")
moa_set_warnings(moa_camera_tests)

include(GoogleTest)
gtest_discover_tests(moa_camera_tests DISCOVERY_MODE PRE_TEST)
