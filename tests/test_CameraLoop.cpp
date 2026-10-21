/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <cmath>

#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "CameraLoopHarness.h"

using namespace moa_mod;
using namespace moa_mod::camera;
using moa_mod::camera::test::CameraLoopHarness;
using moa_mod::camera::test::FakeCameraGame;
using moa_mod::camera::test::KeepTheFeelOfTheOldTests;
using moa_mod::camera::test::ReadTraceFrames;
using moa_mod::camera::test::TraceFrame;

namespace
{

constexpr float k_PitchSensitivity = 30.0f;

/// The settings of the in-game test of the user, with a larger pitch sensitivity so that a sweep fits on the screen.
[[nodiscard]] moa_mod::camera::CameraSettings UserSettings(PitchMode mode)
{
	moa_mod::camera::CameraSettings settings;
	settings.enabled = true;
	settings.pitch = true;
	settings.lookUp = true;
	settings.pitchSmoothing = 0.1;
	settings.pivotOnHand = true;
	settings.smoothZoom = true;
	settings.dragInertia = 0.35;
	KeepTheFeelOfTheOldTests(settings);
	settings.pitchSensitivity = k_PitchSensitivity;
	settings.pitchMode = mode;
	return settings;
}

/// The worlds of the tests.
enum class World
{
	Flat,
	Hills, ///< Hills, and obstacles in front of the camera.
};

[[nodiscard]] const char* Name(World world)
{
	return world == World::Flat ? "flat" : "hills";
}

/// A harness with the camera at a stage, settled for one second with no input.
[[nodiscard]] CameraLoopHarness Settled(PitchMode mode, World world, float stage, bool zoomRotateAboutHand, float deltaTime)
{
	CameraLoopHarness harness(UserSettings(mode));
	auto& game = harness.game;
	game.zoomRotateAboutHand = zoomRotateAboutHand;
	if (world == World::Hills)
	{
		game.ground = [](float x, float z) { return 100.0f + 12.0f * std::sin(x / 37.0f) * std::cos(z / 53.0f); };
	}
	game.mouse = {1100.0f, 420.0f, 0.0f};
	game.Start({0.0f, 0.0f, 0.0f}, stage);
	if (world == World::Hills)
	{
		// Obstacles along the heading, at the height of a camera near the land.
		const auto forward = Rotate(YawRotation(game.direction), WorldPoint {0.0f, 0.0f, 1.0f});
		for (const auto distance : {12.0f, 25.0f, 45.0f, 70.0f, 110.0f})
		{
			const auto at = forward * distance;
			game.obstacles.push_back(
			    {.centre = {at.x, game.GroundAt(at.x, at.z) + 0.5f * game.height + 2.0f, at.z}, .radius = 3.0f});
		}
	}
	for (float time = 0.0f; time < 1.0f; time += deltaTime)
	{
		harness.Frame(deltaTime);
	}
	return harness;
}

/// The largest distance of the camera from a position over some frames.
struct Drift
{
	WorldPoint anchor;
	float largest = 0.0f;
	WorldPoint largestAt;

	void Add(WorldPoint position)
	{
		const auto distance = Length(position - anchor);
		if (distance > largest)
		{
			largest = distance;
			largestAt = position;
		}
	}
};

/// Drag the middle mouse button vertically to change the raise by about `degrees`, at 300 pixels for each second.
void PitchDrag(CameraLoopHarness& harness, float degrees, float deltaTime, Drift& drift)
{
	auto& game = harness.game;
	const auto pixels = degrees * 100.0f / k_PitchSensitivity;
	const auto frames = std::max(1, static_cast<int>(std::ceil(std::abs(pixels) / (300.0f * deltaTime))));
	game.buttons[2] = true;
	for (int frame = 0; frame < frames; ++frame)
	{
		game.mouse.y += pixels / static_cast<float>(frames);
		harness.Frame(deltaTime);
		drift.Add(harness.Shown());
	}
	// Hold the button with no move while the pitch smoothing ends.
	for (float time = 0.0f; time < 0.5f; time += deltaTime)
	{
		harness.Frame(deltaTime);
		drift.Add(harness.Shown());
	}
	game.buttons[2] = false;
}

/// Run a sweep of the pitch from the tilt of the game to 85 degrees, to the flattest view and into the look up, and
/// back to the tilt of the game; then idle frames. Return the drift of the camera.
[[nodiscard]] Drift PitchSweep(CameraLoopHarness& harness, float deltaTime)
{
	Drift drift {.anchor = harness.Shown()};
	PitchDrag(harness, -90.0f, deltaTime, drift);
	PitchDrag(harness, 130.0f, deltaTime, drift);
	PitchDrag(harness, -40.0f, deltaTime, drift);
	for (float time = 0.0f; time < 2.0f; time += deltaTime)
	{
		harness.Frame(deltaTime);
		drift.Add(harness.Shown());
	}
	return drift;
}

/// The horizontal distance of the camera from the hand point.
[[nodiscard]] float DistanceToHand(const CameraLoopHarness& harness)
{
	const auto camera = harness.Shown();
	const auto hand = harness.game.handPoint;
	return std::hypot(camera.x - hand.x, camera.z - hand.z);
}

/// The settings of the mod in the in-game test (the header of the trace).
[[nodiscard]] moa_mod::camera::CameraSettings TraceSettings()
{
	moa_mod::camera::CameraSettings settings;
	settings.enabled = true;
	settings.pivotOnHand = true;
	settings.smoothZoom = true;
	settings.zoomSmoothing = 0.3;
	settings.rotateSmoothing = 0.0;
	settings.wheelZoomStep = 0.5;
	settings.lookUp = true;
	settings.maxLookUpDegrees = 15.0;
	settings.lookUpFullStage = 0.5;
	settings.lookUpZeroStage = 1.5;
	settings.pitchSensitivity = 10.0;
	settings.pitch = true;
	settings.minTiltDegrees = 5.0;
	settings.maxTiltDegrees = 85.0;
	settings.pitchSmoothing = 0.1;
	settings.resetPitchOnDoubleClick = true;
	settings.groundClearance = 1.5;
	settings.pitchMode = PitchMode::InPlace;
	settings.dragInertia = 0.35;
	KeepTheFeelOfTheOldTests(settings);
	settings.dragInertiaMaxSpeed = 15.0;
	return settings;
}

/// The land of the test place, from the land heights in the trace: a hill behind the camera that rises to 116 m, and
/// lower land to the edge of the map at x = 329.
[[nodiscard]] float TraceGround(float x, float /*z*/)
{
	constexpr std::array<std::pair<float, float>, 7> k_Profile {{{-400.0f, 121.0f},
	                                                             {-160.0f, 106.5f},
	                                                             {-20.0f, 103.0f},
	                                                             {15.0f, 112.0f},
	                                                             {110.0f, 116.5f},
	                                                             {240.0f, 102.5f},
	                                                             {330.0f, 90.0f}}};
	if (x <= k_Profile.front().first)
	{
		return k_Profile.front().second;
	}
	for (size_t i = 1; i < k_Profile.size(); ++i)
	{
		if (x <= k_Profile[i].first)
		{
			const auto& [x0, y0] = k_Profile[i - 1];
			const auto& [x1, y1] = k_Profile[i];
			return y0 + (y1 - y0) * (x - x0) / (x1 - x0);
		}
	}
	return k_Profile.back().second;
}

[[nodiscard]] float HorizontalLength(WorldPoint a)
{
	return std::hypot(a.x, a.z);
}

/// What a replay of the trace did.
struct ReplayResult
{
	float largestExcess = -1e9f; ///< The largest move of a frame beyond the move that the frame permits, metres.
	int excessFrame = 0;
	float largestMove = 0.0f; ///< The largest horizontal move of the camera in one frame, metres.
	int moveFrame = 0;
	float largestRoll = 0.0f; ///< The largest roll of the camera, degrees.
	float path = 0.0f;        ///< The horizontal path of the camera, metres.
	int turns = 0;            ///< The count of frames in which the heading changed.
	WorldPoint start;
	WorldPoint end;
	int pushes = 0;          ///< The count of the pushes of the game above the land.
	int zoomRotateHolds = 0; ///< The count of frames of the drag in which the game held the land under the cursor.
};

/// Replay the trace: the recorded input, the turn of the game, the height and the tilt of the game, and the hand.
/// The model of the game does the rest: the hold during a zoom or a turn, the push above the land with its block of
/// the hold, and the clamp into the map. In each frame, the camera can move only by the turn about the hand and by
/// the zoom of the game (the ease of the height and the tilt). With `ease` false, the height and the tilt of the
/// game stay at their values at the start, and the game does not turn.
[[nodiscard]] ReplayResult Replay(const std::vector<TraceFrame>& frames, const moa_mod::camera::CameraSettings& settings,
                                  bool ease)
{
	CameraLoopHarness harness(settings);
	auto& game = harness.game;
	game.screenWidth = 2560.0f;
	game.screenHeight = 1440.0f;
	game.ground = TraceGround;
	game.mapMinimum = -1000.0f;
	game.mapMaximum = 329.0f;
	game.scriptedHand = true;
	const auto& first = frames.front();
	game.direction = first.direction;
	game.StartAt(first.focus, first.stage, {.height = first.height, .tilt = first.tilt});

	ReplayResult result {.start = harness.Shown()};
	auto previous = harness.Shown();
	auto previousDirection = game.direction;
	auto previousHeight = game.height;
	auto previousTilt = game.tilt;
	bool previousMiddle = false;
	for (const auto& frame : frames)
	{
		game.buttons[2] = frame.middle;
		game.mouse = frame.mouse;
		game.wheel = frame.wheel;
		game.handPoint = frame.hand;
		game.cursorOnLand = frame.cursorOnLand;
		const auto turn = ease ? WrapDegrees(frame.gameDirection - frame.direction) : 0.0f;
		game.turnPerSecond = turn / frame.deltaTime;
		game.scriptedStage = ease ? FakeCameraGame::StageRow {.height = frame.gameHeight, .tilt = frame.gameTilt}
		                          : FakeCameraGame::StageRow {.height = first.height, .tilt = first.tilt};
		const auto pushes = game.pushes;
		harness.Frame(frame.deltaTime);
		result.pushes += game.pushes - pushes;
		// The middle button drag and the frame after its release.
		result.zoomRotateHolds += (frame.middle || previousMiddle) && game.lastFrameHeld ? 1 : 0;
		previousMiddle = frame.middle;

		const auto shown = harness.Shown();
		const auto turned = WrapDegrees(game.direction - previousDirection);
		result.turns += std::abs(turned) > 1e-4f ? 1 : 0;
		// The turn about the hand keeps the distance to the hand. The zoom of the game moves the camera by the change
		// of the offset from the focus.
		const auto zoom = GameCameraOffset(game.direction, game.height, game.tilt) -
		                  GameCameraOffset(game.direction, previousHeight, previousTilt);
		const auto permitted =
		    HorizontalLength(previous - frame.hand) * std::abs(turned) * k_DegreesToRadians + HorizontalLength(zoom) + 0.01f;
		const auto move = HorizontalLength(shown - previous);
		result.path += move;
		if (move - permitted > result.largestExcess)
		{
			result.largestExcess = move - permitted;
			result.excessFrame = frame.frame;
		}
		if (move > result.largestMove)
		{
			result.largestMove = move;
			result.moveFrame = frame.frame;
		}
		result.largestRoll = std::max(result.largestRoll, std::abs(RollDegrees(game.transform.rotation)));
		previous = shown;
		previousDirection = game.direction;
		previousHeight = game.height;
		previousTilt = game.tilt;
	}
	result.end = previous;
	return result;
}

[[nodiscard]] std::vector<TraceFrame> PitchDriftTrace()
{
	return ReadTraceFrames(std::string(MOA_TEST_DATA_DIR) + "/camera-trace-pitch-drift.csv");
}

} // namespace

TEST(CameraLoop, PitchInPlaceKeepsTheCameraStill)
{
	for (const auto world : {World::Flat, World::Hills})
	{
		for (const auto stage : {0.0f, 1.0f, 2.0f})
		{
			for (const auto rate : {30, 60, 144})
			{
				for (const auto aboutHand : {true, false})
				{
					SCOPED_TRACE(std::string(Name(world)) + " stage " + std::to_string(stage) + " at " + std::to_string(rate) +
					             " fps, zoom and turn about the hand " + (aboutHand ? "on" : "off"));
					const auto deltaTime = 1.0f / static_cast<float>(rate);
					auto harness = Settled(PitchMode::InPlace, world, stage, aboutHand, deltaTime);
					const auto drift = PitchSweep(harness, deltaTime);
					EXPECT_LE(drift.largest, 0.01f)
					    << "the camera went to (" << drift.largestAt.x << ", " << drift.largestAt.y << ", " << drift.largestAt.z
					    << ") from (" << drift.anchor.x << ", " << drift.anchor.y << ", " << drift.anchor.z << ")";
					EXPECT_TRUE(harness.game.errors.empty());
				}
			}
		}
	}
}

TEST(CameraLoop, PitchInPlaceDoesNotMoveAwayFromTheHand)
{
	// The bug of the in-game test: a flatter view moved the camera back, away from the hand.
	for (const auto aboutHand : {true, false})
	{
		SCOPED_TRACE(aboutHand ? "zoom and turn about the hand on" : "zoom and turn about the hand off");
		constexpr float k_DeltaTime = 1.0f / 60.0f;
		auto harness = Settled(PitchMode::InPlace, World::Flat, 0.0f, aboutHand, k_DeltaTime);
		const auto before = DistanceToHand(harness);
		const auto camera = harness.Shown();
		Drift drift {.anchor = camera};
		PitchDrag(harness, 12.0f, k_DeltaTime, drift);
		EXPECT_LE(drift.largest, 0.01f);
		// The hand moves farther away on the land as the view gets flatter, but the camera does not move.
		EXPECT_NEAR(harness.Shown().x, camera.x, 0.01f);
		EXPECT_NEAR(harness.Shown().z, camera.z, 0.01f);
		EXPECT_GE(DistanceToHand(harness), before);
	}
}

TEST(CameraLoop, PitchInPlaceKeepsTheCameraStillWhenTheCursorCrossesTheUi)
{
	// The game holds the land under the cursor during a middle button drag also when the cursor is over the UI. A
	// vertical drag that crosses a panel must not let that hold pull the camera back.
	for (const auto world : {World::Flat, World::Hills})
	{
		for (const auto stage : {0.0f, 1.0f})
		{
			for (const auto rate : {30, 60, 144})
			{
				const auto deltaTime = 1.0f / static_cast<float>(rate);
				auto harness = Settled(PitchMode::InPlace, world, stage, true, deltaTime);
				auto& game = harness.game;
				Drift drift {.anchor = harness.Shown()};
				game.buttons[2] = true;
				const auto frames = static_cast<int>(1.5f / deltaTime);
				for (int frame = 0; frame < frames; ++frame)
				{
					// The cursor goes up 200 pixels in each second, and it is over a panel in the middle third.
					game.mouse.y += 200.0f * deltaTime;
					game.handOverUi = frame > frames / 3 && frame < 2 * frames / 3;
					harness.Frame(deltaTime);
					drift.Add(harness.Shown());
				}
				game.handOverUi = false;
				game.buttons[2] = false;
				for (float time = 0.0f; time < 1.0f; time += deltaTime)
				{
					harness.Frame(deltaTime);
					drift.Add(harness.Shown());
				}
				EXPECT_LT(drift.largest, 0.01f)
				    << Name(world) << " stage " << stage << " at " << rate << " fps: the camera went to (" << drift.largestAt.x
				    << ", " << drift.largestAt.y << ", " << drift.largestAt.z << ")";
			}
		}
	}
}

TEST(CameraLoop, PitchInPlaceKeepsTheCameraStillWhenTheGameSeesTheReleaseLate)
{
	// The game reads the buttons through its own input. If it sees the release one frame late, it holds the land in
	// that frame unless the loop blocks it.
	for (const auto rate : {30, 60, 144})
	{
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		auto harness = Settled(PitchMode::InPlace, World::Flat, 0.0f, true, deltaTime);
		auto& game = harness.game;
		game.lateMiddleRelease = true;
		Drift drift {.anchor = harness.Shown()};
		for (int drag = 0; drag < 4; ++drag)
		{
			game.buttons[2] = true;
			for (float time = 0.0f; time < 0.3f; time += deltaTime)
			{
				game.mouse.y += (drag % 2 == 0 ? 300.0f : -300.0f) * deltaTime;
				harness.Frame(deltaTime);
				drift.Add(harness.Shown());
			}
			// Release while the cursor moves.
			game.buttons[2] = false;
			for (float time = 0.0f; time < 0.3f; time += deltaTime)
			{
				game.mouse.y += (drag % 2 == 0 ? 300.0f : -300.0f) * deltaTime;
				harness.Frame(deltaTime);
				drift.Add(harness.Shown());
			}
		}
		EXPECT_LT(drift.largest, 0.01f) << rate << " fps: the camera went to (" << drift.largestAt.x << ", "
		                                << drift.largestAt.y << ", " << drift.largestAt.z << ")";
	}
}

TEST(CameraLoop, ZoomTurnAndDragWorkAfterAPitchInPlace)
{
	constexpr float k_DeltaTime = 1.0f / 60.0f;
	auto harness = Settled(PitchMode::InPlace, World::Flat, 1.0f, true, k_DeltaTime);
	Drift drift {.anchor = harness.Shown()};
	PitchDrag(harness, 10.0f, k_DeltaTime, drift);
	auto& game = harness.game;

	// The wheel zooms out along the view direction.
	const auto beforeZoom = harness.Shown();
	game.wheel = -0.1f;
	harness.Frame(k_DeltaTime);
	game.wheel = 0.0f;
	for (float time = 0.0f; time < 2.0f; time += k_DeltaTime)
	{
		harness.Frame(k_DeltaTime);
	}
	EXPECT_GT(game.stage, 1.4f);
	EXPECT_GT(harness.Shown().y - beforeZoom.y, 5.0f);

	// The keys turn the camera.
	const auto beforeTurn = game.direction;
	game.turnPerSecond = 60.0f;
	for (float time = 0.0f; time < 0.5f; time += k_DeltaTime)
	{
		harness.Frame(k_DeltaTime);
	}
	game.turnPerSecond = 0.0f;
	EXPECT_GT(std::abs(WrapDegrees(game.direction - beforeTurn)), 20.0f);

	// The right mouse button drags the land.
	const auto beforeDrag = game.focus;
	game.buttons[1] = true;
	for (int frame = 0; frame < 30; ++frame)
	{
		game.mouse.y += 8.0f;
		harness.Frame(k_DeltaTime);
	}
	game.buttons[1] = false;
	for (float time = 0.0f; time < 3.0f; time += k_DeltaTime)
	{
		harness.Frame(k_DeltaTime);
	}
	EXPECT_GT(Length(game.focus - beforeDrag), 5.0f);
	EXPECT_TRUE(game.errors.empty());
}

TEST(CameraLoop, PitchInOrbitModeMovesTheCamera)
{
	constexpr float k_DeltaTime = 1.0f / 60.0f;
	auto harness = Settled(PitchMode::Orbit, World::Flat, 1.0f, false, k_DeltaTime);
	const auto drift = PitchSweep(harness, k_DeltaTime);
	EXPECT_GT(drift.largest, 1.0f);
}

TEST(CameraLoop, LandDragHoldsTheGrabbedPointInAPitchedView)
{
	for (const auto mode : {PitchMode::InPlace, PitchMode::Orbit})
	{
		for (const auto rate : {30, 60, 144})
		{
			SCOPED_TRACE(std::string(mode == PitchMode::InPlace ? "in place" : "orbit") + " at " + std::to_string(rate) +
			             " fps");
			const auto deltaTime = 1.0f / static_cast<float>(rate);
			auto harness = Settled(mode, World::Flat, 0.0f, true, deltaTime);
			Drift drift {.anchor = harness.Shown()};
			PitchDrag(harness, 10.0f, deltaTime, drift);
			auto& game = harness.game;
			const auto grab = game.LandAt(game.transform, game.mouse);
			ASSERT_TRUE(grab);
			game.buttons[1] = true;
			harness.Frame(deltaTime);
			float largest = 0.0f;
			for (float time = 0.0f; time < 0.6f; time += deltaTime)
			{
				game.mouse.y += 200.0f * deltaTime;
				game.mouse.x -= 150.0f * deltaTime;
				harness.Frame(deltaTime);
				// The grabbed land stays under the hold cursor of the game in the view of the user.
				const auto under = game.LandAt(game.transform, game.drag.holdCursor);
				ASSERT_TRUE(under);
				largest = std::max(largest, std::hypot(under->x - grab->x, under->z - grab->z));
			}
			EXPECT_LE(largest, 0.05f);
			EXPECT_GT(Length(game.focus - drift.anchor), 1.0f);
		}
	}
}

TEST(CameraLoop, GlideContinuesTheDragAndStops)
{
	for (const auto rate : {30, 60, 144})
	{
		SCOPED_TRACE(std::to_string(rate) + " fps");
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		auto harness = Settled(PitchMode::InPlace, World::Flat, 1.0f, true, deltaTime);
		auto& game = harness.game;
		game.buttons[1] = true;
		for (float time = 0.0f; time < 0.4f; time += deltaTime)
		{
			game.mouse.x += 600.0f * deltaTime;
			harness.Frame(deltaTime);
		}
		game.buttons[1] = false;
		const auto released = game.focus;
		for (float time = 0.0f; time < 0.3f; time += deltaTime)
		{
			harness.Frame(deltaTime);
		}
		EXPECT_GT(Length(game.focus - released), 1.0f);
		for (float time = 0.0f; time < 4.0f; time += deltaTime)
		{
			harness.Frame(deltaTime);
		}
		const auto stopped = game.focus;
		for (float time = 0.0f; time < 0.5f; time += deltaTime)
		{
			harness.Frame(deltaTime);
		}
		EXPECT_LE(Length(game.focus - stopped), 0.01f);
	}
}

TEST(CameraLoopReplay, PitchAfterAZoomMovesTheCameraOnlyByTheTurnAndTheZoom)
{
	// The trace of the in-game test of 2026-10-19: the user zoomed in with the wheel and then pitched up to the horizon
	// and back down, with small turns. The old loop moved the camera 87 to 473 m in single frames.
	const auto frames = PitchDriftTrace();
	ASSERT_EQ(frames.size(), 22U);
	for (const auto lock : {false, true})
	{
		SCOPED_TRACE(lock ? "turn lock on" : "turn lock off");
		auto settings = TraceSettings();
		settings.pitchTurnLock = lock;
		const auto result = Replay(frames, settings, true);
		std::cout << (lock ? "turn lock on" : "turn lock off") << ": path " << result.path << " m, largest move "
		          << result.largestMove << " m at frame " << result.moveFrame << ", net "
		          << HorizontalLength(result.end - result.start) << " m, turns " << result.turns << ", pushes " << result.pushes
		          << '\n';
		EXPECT_LE(result.largestExcess, 0.0f)
		    << "frame " << result.excessFrame << " moved the camera " << result.largestExcess
		    << " m more than the turn and the zoom permit; the largest move was " << result.largestMove << " m at frame "
		    << result.moveFrame << ", the path " << result.path << " m";
		EXPECT_LT(result.largestRoll, 1e-3f);
		// The game pushed its camera above the hill, and the push blocked its own hold. The mod blocked it in the
		// other frames of the drag.
		EXPECT_GT(result.pushes, 0);
		EXPECT_EQ(result.zoomRotateHolds, 0);
		// The game turned in 7 frames. With the lock, only the turn of a frame with a large enough horizontal move
		// stays: the frame that moved the mouse 1 pixel to the left and 1 pixel down.
		EXPECT_EQ(result.turns, lock ? 1 : 7);
	}
}

TEST(CameraLoopReplay, PitchWithNoZoomKeepsTheCameraStill)
{
	// The same input with no ease of the zoom and no turn of the game: the camera does not move.
	const auto frames = PitchDriftTrace();
	ASSERT_FALSE(frames.empty());
	const auto result = Replay(frames, TraceSettings(), false);
	EXPECT_LE(HorizontalLength(result.end - result.start), 0.01f);
	EXPECT_LE(result.largestMove, 0.01f) << "frame " << result.moveFrame;
	EXPECT_LT(result.largestRoll, 1e-3f);
}
