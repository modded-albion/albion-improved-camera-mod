/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <cmath>

#include <algorithm>
#include <array>
#include <iostream>
#include <optional>
#include <string>
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
/// The largest move of the pivot on the screen that the tests accept, pixels.
constexpr float k_PivotPixels = 0.5f;

/// The settings of the hand rig, with a larger pitch sensitivity so that a sweep fits on the screen.
[[nodiscard]] moa_mod::camera::CameraSettings HandSettings()
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
	settings.pitchMode = PitchMode::Hand;
	return settings;
}

enum class World
{
	Flat,
	Hills,
	MapEdge, ///< Flat land, with the focus 60 metres from the edge of the map.
};

[[nodiscard]] const char* Name(World world)
{
	switch (world)
	{
	case World::Flat:
		return "flat";
	case World::Hills:
		return "hills";
	case World::MapEdge:
		return "map edge";
	}
	return "";
}

/// A cursor near the bottom of the screen (land near the camera) and one near the top (land far away).
enum class Cursor
{
	Near,
	Far,
};

[[nodiscard]] WorldPoint CursorAt(Cursor cursor)
{
	return cursor == Cursor::Near ? WorldPoint {1100.0f, 300.0f, 0.0f} : WorldPoint {800.0f, 760.0f, 0.0f};
}

/// A harness with the hand rig at a stage, settled for one second with no input.
[[nodiscard]] CameraLoopHarness Settled(World world, float stage, Cursor cursor, float deltaTime,
                                        const moa_mod::camera::CameraSettings& settings = HandSettings())
{
	CameraLoopHarness harness(settings);
	auto& game = harness.game;
	game.focusHeightEase = true;
	if (world == World::Hills)
	{
		game.ground = [](float x, float z) { return 100.0f + 12.0f * std::sin(x / 37.0f) * std::cos(z / 53.0f); };
	}
	WorldPoint focus {};
	if (world == World::MapEdge)
	{
		game.mapMaximum = 60.0f;
	}
	game.mouse = CursorAt(cursor);
	game.Start(focus, stage);
	for (float time = 0.0f; time < 1.0f; time += deltaTime)
	{
		harness.Frame(deltaTime);
	}
	return harness;
}

/// The place of a world point on the screen in the camera that the user sees.
[[nodiscard]] std::optional<Vector2> ScreenOf(const CameraLoopHarness& harness, WorldPoint point)
{
	return ScreenPoint(harness.game.ScreenCameraOf(harness.game.transform), point);
}

/// What a gesture did to the camera.
struct GestureResult
{
	float largestDrift = 0.0f; ///< The largest move of the pivot on the screen in the frames that kept it, pixels.
	int frames = 0;
	int keptFrames = 0;       ///< The frames in which the rig kept the pivot at its place.
	float largestRoll = 0.0f; ///< Degrees.
	float gamePath = 0.0f;    ///< The path of the camera in the frames of the game, metres.
	float loopPath = 0.0f;    ///< The path of the camera by the loop, metres.
	float shownPath = 0.0f;   ///< The path of the camera that the user saw, metres.
	float lowest = 1e9f;      ///< The lowest height of the camera above the land that the game keeps it above.
	/// The frames of a hold of the middle button, after the press, in which the desired distance changed with no zoom
	/// (changes 2, 4 and 5; the black box gives an invariant for them).
	int heldDesiredChanges = 0;
	int clampChanges = 0; ///< The frames with a new arm after the clamp into the map (change 6).
	bool lastHeld = false;
};

/// Follow the pivot of the gesture over frames. The pivot is the land under the cursor at the start.
struct PivotWatch
{
	WorldPoint pivot;
	Vector2 screen;
	GestureResult result;

	void Start(const CameraLoopHarness& harness)
	{
		const auto& game = harness.game;
		pivot = game.LandAt(game.transform, game.mouse).value_or(WorldPoint {});
		screen = ScreenOf(harness, pivot).value_or(Vector2 {});
	}

	void Add(const CameraLoopHarness& harness)
	{
		const auto& game = harness.game;
		++result.frames;
		if (harness.loop.Trace().pivotKept)
		{
			++result.keptFrames;
			if (auto at = ScreenOf(harness, pivot))
			{
				result.largestDrift = std::max(result.largestDrift, std::hypot(at->x - screen.x, at->y - screen.y));
			}
			else
			{
				result.largestDrift = 1e9f;
			}
		}
		else if (auto at = ScreenOf(harness, pivot))
		{
			// The rig moved the pivot on the screen (the land or the edge of the map). Follow it from its new place.
			screen = *at;
		}
		result.largestRoll = std::max(result.largestRoll, std::abs(RollDegrees(game.transform.rotation)));
		const auto change = harness.loop.Trace().desiredChange;
		if (game.buttons[2] && result.lastHeld && (change == 2 || change == 4 || change == 5))
		{
			++result.heldDesiredChanges;
		}
		result.clampChanges += change == 6 ? 1 : 0;
		result.lastHeld = game.buttons[2];
		result.gamePath += harness.GameMove();
		result.loopPath += harness.LoopMove();
		result.shownPath += harness.ShownMove();
		const auto position = game.transform.position;
		// The floor of the spring arm is the exact land plus the clearance.
		const auto ground = game.GroundAt(position.x, position.z);
		result.lowest = std::min(result.lowest, position.y - ground);
	}
};

/// Press the middle mouse button, drag it by the pixels at 300 pixels for each second, then hold it for 0.5 s.
void MiddleDrag(CameraLoopHarness& harness, float pixelsX, float pixelsY, float deltaTime, PivotWatch& watch,
                float turnPerSecond = 0.0f)
{
	auto& game = harness.game;
	const auto length = std::hypot(pixelsX, pixelsY);
	const auto frames = std::max(1, static_cast<int>(std::ceil(length / (300.0f * deltaTime))));
	// The press, with no move: the rig takes the land under the cursor as the pivot.
	game.buttons[2] = true;
	harness.Frame(deltaTime);
	watch.Add(harness);
	game.turnPerSecond = turnPerSecond;
	for (int frame = 0; frame < frames; ++frame)
	{
		game.mouse.x += pixelsX / static_cast<float>(frames);
		game.mouse.y += pixelsY / static_cast<float>(frames);
		harness.Frame(deltaTime);
		watch.Add(harness);
	}
	game.turnPerSecond = 0.0f;
	for (float time = 0.0f; time < 0.5f; time += deltaTime)
	{
		harness.Frame(deltaTime);
		watch.Add(harness);
	}
	game.buttons[2] = false;
}

void Idle(CameraLoopHarness& harness, float seconds, float deltaTime, PivotWatch& watch)
{
	for (float time = 0.0f; time < seconds; time += deltaTime)
	{
		harness.Frame(deltaTime);
		watch.Add(harness);
	}
}

[[nodiscard]] std::string Describe(World world, float stage, int rate, Cursor cursor)
{
	return std::string(Name(world)) + ", stage " + std::to_string(stage) + ", " + std::to_string(rate) + " fps, " +
	       (cursor == Cursor::Near ? "hand near" : "hand far");
}

constexpr std::array k_Worlds {World::Flat, World::Hills, World::MapEdge};
constexpr std::array k_Stages {0.0f, 1.0f, 2.0f, 4.0f};
constexpr std::array k_Rates {30, 60, 144};
constexpr std::array k_Cursors {Cursor::Near, Cursor::Far};

} // namespace

TEST(HandRig, PitchKeepsTheHandPointOnTheScreen)
{
	for (const auto world : k_Worlds)
	{
		for (const auto stage : k_Stages)
		{
			for (const auto rate : k_Rates)
			{
				for (const auto cursor : k_Cursors)
				{
					SCOPED_TRACE(Describe(world, stage, rate, cursor));
					const auto deltaTime = 1.0f / static_cast<float>(rate);
					auto harness = Settled(world, stage, cursor, deltaTime);
					PivotWatch watch;
					watch.Start(harness);
					// Steeper by 30 degrees, flatter by 40 degrees, and back.
					const auto startPitch = PitchOf(harness.game.transform.rotation);
					MiddleDrag(harness, 0.0f, -100.0f, deltaTime, watch);
					const auto steepest = PitchOf(harness.game.transform.rotation);
					MiddleDrag(harness, 0.0f, 133.0f, deltaTime, watch);
					const auto flattest = PitchOf(harness.game.transform.rotation);
					MiddleDrag(harness, 0.0f, -33.0f, deltaTime, watch);
					Idle(harness, 1.0f, deltaTime, watch);
					const auto& result = watch.result;
					// The drag turned the view 30 degrees steeper and then 40 degrees flatter.
					EXPECT_NEAR(steepest - startPitch, 30.0f, 0.2f);
					EXPECT_NEAR(steepest - flattest, 39.9f, 0.2f);
					EXPECT_GT(result.keptFrames, result.frames * 3 / 4);
					EXPECT_LE(result.largestDrift, k_PivotPixels);
					EXPECT_LT(result.largestRoll, 1e-3f);
					EXPECT_GE(result.lowest, 1.5f - 1e-3f);
					EXPECT_EQ(result.heldDesiredChanges, 0);
					EXPECT_TRUE(harness.game.errors.empty());
					// No frame of the game moves the camera: the rig owns each move.
					EXPECT_LE(result.gamePath, 0.01f + 0.01f * result.shownPath)
					    << "game " << result.gamePath << " m, loop " << result.loopPath << " m, shown " << result.shownPath;
					EXPECT_LE(result.loopPath, result.shownPath * 1.01f + 0.01f);
				}
			}
		}
	}
}

TEST(HandRig, AHoldAtTheMapEdgeDoesNotTakeANewPivot)
{
	// Camera test reset-view in the game: at the edge of the map the clamp of the game moved the camera and the arm,
	// and the next frame of the hold made a new arm. That changed the desired distance with the reason of a new pivot,
	// and the black box gave the invariant "desired change while held". The new arm now has its own reason (6).
	int clampChanges = 0;
	for (const auto stage : k_Stages)
	{
		for (const auto rate : k_Rates)
		{
			for (const auto cursor : k_Cursors)
			{
				SCOPED_TRACE(Describe(World::MapEdge, stage, rate, cursor));
				const auto deltaTime = 1.0f / static_cast<float>(rate);
				auto harness = Settled(World::MapEdge, stage, cursor, deltaTime);
				PivotWatch watch;
				watch.Start(harness);
				// A pitch to the horizon with a turn, as in the hold of reset-view.
				MiddleDrag(harness, 300.0f, 200.0f, deltaTime, watch, 90.0f);
				Idle(harness, 1.0f, deltaTime, watch);
				EXPECT_EQ(watch.result.heldDesiredChanges, 0);
				EXPECT_TRUE(harness.game.errors.empty());
				clampChanges += watch.result.clampChanges;
			}
		}
	}
	std::cout << clampChanges << " new arms after the clamp into the map" << std::endl;
}

TEST(HandRig, TurnKeepsTheHandPointOnTheScreen)
{
	for (const auto world : k_Worlds)
	{
		for (const auto stage : k_Stages)
		{
			for (const auto rate : k_Rates)
			{
				for (const auto cursor : k_Cursors)
				{
					SCOPED_TRACE(Describe(world, stage, rate, cursor));
					const auto deltaTime = 1.0f / static_cast<float>(rate);
					auto harness = Settled(world, stage, cursor, deltaTime);
					PivotWatch watch;
					watch.Start(harness);
					// A horizontal middle drag: the game turns 90 degrees for each second.
					MiddleDrag(harness, 150.0f, 0.0f, deltaTime, watch, 90.0f);
					EXPECT_LE(watch.result.largestDrift, k_PivotPixels);
					EXPECT_GT(watch.result.keptFrames, watch.result.frames / 2);
					// A turn with the keys and no button, after the end of the drag. It is a new gesture with a new pivot.
					harness.Frame(deltaTime);
					watch.Start(harness);
					harness.game.turnPerSecond = -60.0f;
					Idle(harness, 0.5f, deltaTime, watch);
					harness.game.turnPerSecond = 0.0f;
					Idle(harness, 0.5f, deltaTime, watch);
					EXPECT_LE(watch.result.largestDrift, k_PivotPixels);
					EXPECT_LT(watch.result.largestRoll, 1e-3f);
					// While the button is held, only a zoom or the clamp into the map gives a new desired distance.
					EXPECT_EQ(watch.result.heldDesiredChanges, 0);
					if (watch.result.clampChanges > 0)
					{
						std::cout << Describe(world, stage, rate, cursor) << ": " << watch.result.clampChanges
						          << " new arms after the clamp into the map" << std::endl;
					}
					EXPECT_TRUE(harness.game.errors.empty());
				}
			}
		}
	}
}

TEST(HandRig, ZoomKeepsTheHandPointOnTheScreen)
{
	for (const auto world : k_Worlds)
	{
		for (const auto stage : k_Stages)
		{
			for (const auto rate : k_Rates)
			{
				for (const auto cursor : k_Cursors)
				{
					SCOPED_TRACE(Describe(world, stage, rate, cursor));
					const auto deltaTime = 1.0f / static_cast<float>(rate);
					auto harness = Settled(world, stage, cursor, deltaTime);
					auto& game = harness.game;
					PivotWatch watch;
					watch.Start(harness);
					const auto startDistance = Length(game.transform.position - watch.pivot);
					// Two notches in (where the stage permits) and four out, then the smoothing ends.
					for (const auto notch : {0.1f, 0.1f, -0.1f, -0.1f, -0.1f, -0.1f})
					{
						// Each notch takes the land under the cursor as the pivot again.
						watch.Start(harness);
						game.wheel = notch;
						harness.Frame(deltaTime);
						watch.Add(harness);
						game.wheel = 0.0f;
						Idle(harness, 0.15f, deltaTime, watch);
					}
					Idle(harness, 1.5f, deltaTime, watch);
					const auto& result = watch.result;
					EXPECT_LT(result.largestRoll, 1e-3f);
					EXPECT_TRUE(game.errors.empty());
					// At the map edge the clamp moves the rig, and the camera that the user sees follows it with the chase
					// (decision of 2026-10-20): the hand point moves on the screen while the chase runs, and the game places
					// its camera at the target. `MapEdge.AZoomAtTheEdgeOfTheMapNeverThrowsTheCameraOrPutsItUnderTheLand` tests
					// that case.
					if (world != World::MapEdge)
					{
						EXPECT_LE(result.largestDrift, k_PivotPixels);
						EXPECT_LE(result.gamePath, 0.01f + 0.01f * result.shownPath)
						    << "game " << result.gamePath << " m, loop " << result.loopPath << " m, shown " << result.shownPath;
					}
					if (stage < 4.0f)
					{
						EXPECT_GT(Length(game.transform.position - watch.pivot), startDistance);
					}
				}
			}
		}
	}
}

TEST(HandRig, LooksUpNearTheGroundWithNoStep)
{
	for (const auto world : {World::Flat, World::Hills})
	{
		for (const auto rate : k_Rates)
		{
			SCOPED_TRACE(std::string(Name(world)) + ", " + std::to_string(rate) + " fps");
			const auto deltaTime = 1.0f / static_cast<float>(rate);
			auto harness = Settled(world, 0.0f, Cursor::Near, deltaTime);
			auto& game = harness.game;
			PivotWatch watch;
			watch.Start(harness);
			// Drag up by 40 degrees: the orbit about the hand stops at the land, and the view turns up in place.
			game.buttons[2] = true;
			harness.Frame(deltaTime);
			auto last = PitchOf(game.transform.rotation);
			float largestStep = 0.0f;
			for (float time = 0.0f; time < 1.0f; time += deltaTime)
			{
				game.mouse.y += 133.0f * deltaTime;
				harness.Frame(deltaTime);
				watch.Add(harness);
				const auto pitch = PitchOf(game.transform.rotation);
				largestStep = std::max(largestStep, std::abs(pitch - last));
				last = pitch;
			}
			game.buttons[2] = false;
			Idle(harness, 1.0f, deltaTime, watch);
			// The view centre goes above the horizon, the camera stays above the land, and no frame jumps.
			EXPECT_LT(PitchOf(game.transform.rotation), 0.0f);
			EXPECT_GE(watch.result.lowest, 1.5f - 1e-3f);
			EXPECT_LT(largestStep, 2.0f);
			EXPECT_LE(watch.result.largestDrift, k_PivotPixels);
			EXPECT_LT(watch.result.largestRoll, 1e-3f);
		}
	}
}

TEST(HandRig, LandDragHoldsTheGrabbedPointAfterAPitch)
{
	// The land drag of the game, with the land grip of the rig off: the game holds the land and the loop adds no move.
	// The test `LandGrip.HoldsTheGrippedLandAfterAPitch` checks the land grip.
	auto settings = HandSettings();
	settings.landGrip = false;
	settings.chaseSeconds = 0.0;
	for (const auto rate : k_Rates)
	{
		SCOPED_TRACE(std::to_string(rate) + " fps");
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		auto harness = Settled(World::Hills, 0.0f, Cursor::Near, deltaTime, settings);
		auto& game = harness.game;
		PivotWatch watch;
		watch.Start(harness);
		MiddleDrag(harness, 0.0f, -30.0f, deltaTime, watch);
		Idle(harness, 0.5f, deltaTime, watch);
		const auto grab = game.LandAt(game.transform, game.mouse);
		ASSERT_TRUE(grab);
		const auto start = game.transform.position;
		game.buttons[1] = true;
		harness.Frame(deltaTime);
		float largest = 0.0f;
		float loopPath = 0.0f;
		for (float time = 0.0f; time < 0.6f; time += deltaTime)
		{
			game.mouse.y += 200.0f * deltaTime;
			game.mouse.x -= 150.0f * deltaTime;
			harness.Frame(deltaTime);
			loopPath += harness.LoopMove();
			// The game holds the grabbed land under its hold cursor, in the camera that the user sees.
			const auto under = game.LandAt(game.transform, game.drag.holdCursor);
			ASSERT_TRUE(under);
			largest = std::max(largest, std::hypot(under->x - grab->x, under->z - grab->z));
			EXPECT_LT(std::abs(RollDegrees(game.transform.rotation)), 1e-3f);
		}
		EXPECT_LE(largest, 0.05f);
		EXPECT_GT(Length(game.transform.position - start), 1.0f);
		// The game made the move of the drag and the rig took it. The loop adds no move.
		EXPECT_LT(loopPath, 0.01f);
		game.buttons[1] = false;
	}
}

TEST(HandRig, GlideContinuesTheDragAndStops)
{
	for (const auto rate : k_Rates)
	{
		SCOPED_TRACE(std::to_string(rate) + " fps");
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		auto harness = Settled(World::Flat, 1.0f, Cursor::Near, deltaTime);
		auto& game = harness.game;
		game.buttons[1] = true;
		for (float time = 0.0f; time < 0.4f; time += deltaTime)
		{
			game.mouse.x += 600.0f * deltaTime;
			harness.Frame(deltaTime);
		}
		game.buttons[1] = false;
		const auto released = game.transform.position;
		for (float time = 0.0f; time < 0.3f; time += deltaTime)
		{
			harness.Frame(deltaTime);
		}
		EXPECT_GT(Length(game.transform.position - released), 1.0f);
		for (float time = 0.0f; time < 4.0f; time += deltaTime)
		{
			harness.Frame(deltaTime);
		}
		const auto stopped = game.transform.position;
		for (float time = 0.0f; time < 0.5f; time += deltaTime)
		{
			harness.Frame(deltaTime);
		}
		EXPECT_LE(Length(game.transform.position - stopped), 0.01f);
		EXPECT_TRUE(game.errors.empty());
	}
}

TEST(HandRig, KeepsTheStateOfTheGameAtTheCameraOfTheUser)
{
	// After each frame, the game places its camera from its own state exactly at the camera of the user.
	constexpr float k_DeltaTime = 1.0f / 60.0f;
	auto harness = Settled(World::Hills, 1.0f, Cursor::Far, k_DeltaTime);
	auto& game = harness.game;
	PivotWatch watch;
	watch.Start(harness);
	MiddleDrag(harness, 80.0f, -60.0f, k_DeltaTime, watch, 40.0f);
	game.wheel = -0.1f;
	harness.Frame(k_DeltaTime);
	game.wheel = 0.0f;
	for (float time = 0.0f; time < 1.0f; time += k_DeltaTime)
	{
		harness.Frame(k_DeltaTime);
		const CameraRig rig {.focus = game.focus,
		                     .direction = game.direction,
		                     .tilt = game.tilt,
		                     .distance = StageDistance(game.height, game.tilt)};
		EXPECT_LT(Length(RigPosition(rig) - game.transform.position), 1e-3f);
		const auto turn = Multiply(RigRotation(rig), Inverse(game.transform.rotation));
		EXPECT_GT(std::abs(turn.w), 1.0f - 1e-6f);
	}
}

namespace
{

/// The settings of the mod in the in-game test of run 5 (the header of the trace), with a pitch mode.
[[nodiscard]] moa_mod::camera::CameraSettings Run5Settings(PitchMode mode)
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
	settings.pitchSensitivity = 10.0;
	settings.pitch = true;
	settings.minTiltDegrees = 5.0;
	settings.maxTiltDegrees = 85.0;
	settings.pitchSmoothing = 0.1;
	settings.groundClearance = 1.5;
	settings.pitchMode = mode;
	settings.pitchTurnLock = true;
	settings.dragInertia = 0.35;
	KeepTheFeelOfTheOldTests(settings);
	settings.dragInertiaMaxSpeed = 15.0;
	return settings;
}

/// The paths of the camera in a replay of a trace.
struct FightResult
{
	float gamePath = 0.0f;  ///< The path of the camera in the frames of the game, metres.
	float loopPath = 0.0f;  ///< The path of the camera by the loop, metres.
	float shownPath = 0.0f; ///< The path of the camera that the user saw, metres.
	float net = 0.0f;       ///< The distance from the first camera to the last camera, metres.
	float largestRoll = 0.0f;
};

/// Replay a drag of a trace: the recorded mouse, buttons, wheel and turn of the game. The model of the game does the
/// rest, with its ease of the height after a zoom and its hold of the land during a zoom or a turn. Before the replay,
/// a vertical middle drag gives the view the raise that it had in the run, with the height of the game held.
[[nodiscard]] FightResult ReplayRun(const std::vector<TraceFrame>& frames, PitchMode mode, float raise)
{
	const auto settings = Run5Settings(mode);
	CameraLoopHarness harness(settings);
	auto& game = harness.game;
	game.screenWidth = 2560.0f;
	game.screenHeight = 1440.0f;
	game.focusHeightEase = true;
	const auto& first = frames.front();
	const auto level = first.focus.y;
	game.ground = [level](float, float) { return level; };
	game.direction = first.direction;
	game.StartAt(first.focus, first.stage, {.height = first.height, .tilt = first.tilt});
	game.mouse = first.mouse;

	constexpr float k_DeltaTime = 1.0f / 60.0f;
	game.scriptedStage = FakeCameraGame::StageRow {.height = first.height, .tilt = first.tilt};
	game.buttons[2] = true;
	harness.Frame(k_DeltaTime);
	const auto pixels = raise * 100.0f / static_cast<float>(settings.pitchSensitivity);
	for (int frame = 0; frame < 20; ++frame)
	{
		game.mouse.y += pixels / 20.0f;
		harness.Frame(k_DeltaTime);
	}
	game.buttons[2] = false;
	for (float time = 0.0f; time < 0.5f; time += k_DeltaTime)
	{
		harness.Frame(k_DeltaTime);
	}
	game.scriptedStage.reset();

	FightResult result;
	const auto start = harness.Shown();
	auto lastMouse = frames.front().mouse;
	game.mouse = lastMouse;
	for (const auto& frame : frames)
	{
		game.buttons[2] = frame.middle;
		// The move of the mouse of the user. With the cursor lock, the cursor starts each frame at the pivot.
		game.mouse = game.mouse + (frame.mouse - lastMouse);
		lastMouse = frame.mouse;
		game.wheel = frame.wheel;
		game.turnPerSecond = WrapDegrees(frame.gameDirection - frame.direction) / frame.deltaTime;
		harness.Frame(frame.deltaTime);
		result.gamePath += harness.GameMove();
		result.loopPath += harness.LoopMove();
		result.shownPath += harness.ShownMove();
		result.largestRoll = std::max(result.largestRoll, std::abs(RollDegrees(game.transform.rotation)));
	}
	result.net = Length(harness.Shown() - start);
	return result;
}

} // namespace

TEST(HandRigReplay, Run5DragsHaveNoFightBetweenTheGameAndTheMod)
{
	// Two middle drags of run 5 (2026-10-19): a zoom out after a steep pitch, and a drag at Detail while the height
	// eased after a zoom in. With the old loop, the game and the mod moved the camera by large opposite amounts in each
	// frame.
	struct Drag
	{
		const char* file;
		float raise; ///< The raise of the view at the start of the drag in the run, degrees.
	};
	for (const auto& drag : {Drag {"camera-trace-run5-zoom-out.csv", -27.8f}, Drag {"camera-trace-run5-detail.csv", -0.7f}})
	{
		SCOPED_TRACE(drag.file);
		const auto frames = ReadTraceFrames(std::string(MOA_TEST_DATA_DIR) + "/" + drag.file);
		ASSERT_GT(frames.size(), 50U);
		const auto old = ReplayRun(frames, PitchMode::InPlace, drag.raise);
		const auto rig = ReplayRun(frames, PitchMode::Hand, drag.raise);
		std::cout << drag.file << ": old loop: game " << old.gamePath << " m, loop " << old.loopPath << " m, shown "
		          << old.shownPath << " m, net " << old.net << " m; hand rig: game " << rig.gamePath << " m, loop "
		          << rig.loopPath << " m, shown " << rig.shownPath << " m, net " << rig.net << " m" << std::endl;
		// The hand rig: each move of the camera of the user has one owner, so the paths of the game and the loop add up
		// to the path that the user saw. The rest is the turn of the game about its focus in the frames with a turn,
		// which the rig replaces with its turn about the pivot.
		EXPECT_LE(rig.gamePath + rig.loopPath, rig.shownPath * 1.1f + 0.05f);
		EXPECT_LE(rig.gamePath, rig.shownPath * 0.05f + 0.05f);
		EXPECT_LT(rig.largestRoll, 1e-3f);
	}
}

namespace
{

/// The game turns by this angle for each pixel of a horizontal middle button drag in the tests of the cursor lock.
constexpr float k_TurnDegreesPerPixel = 0.3f;

[[nodiscard]] bool SamePoint(WorldPoint a, WorldPoint b)
{
	return a.x == b.x && a.y == b.y && a.z == b.z;
}

/// The distance on the screen from a point of the screen to a world point, in the camera that the user sees.
[[nodiscard]] float PixelsTo(const CameraLoopHarness& harness, WorldPoint screen, WorldPoint world)
{
	auto at = ScreenOf(harness, world);
	return at ? std::hypot(at->x - screen.x, at->y - screen.y) : 1e9f;
}

/// What the cursor lock did during a hold of the middle button.
struct LockWatch
{
	std::optional<WorldPoint> pivot;
	int pivots = 0;                   ///< The count of the pivots of the hold.
	float largestCursorPixels = 0.0f; ///< The largest distance from the cursor to the pivot on the screen.
	float largestHandPixels = 0.0f;   ///< The largest distance from the hand to the pivot on the screen.
	float userX = 0.0f;               ///< The sum of the horizontal moves of the mouse of the user, pixels.

	void Add(const CameraLoopHarness& harness)
	{
		const auto& trace = harness.loop.Trace();
		if (!trace.pivot)
		{
			pivots += 1000;
			return;
		}
		if (!pivot || !SamePoint(*pivot, *trace.pivot))
		{
			++pivots;
			pivot = trace.pivot;
		}
		largestCursorPixels = std::max(largestCursorPixels, PixelsTo(harness, harness.game.mouse, *pivot));
		largestHandPixels = std::max(largestHandPixels, PixelsTo(harness, harness.game.handScreen, *pivot));
	}
};

/// Move the mouse of the user by (x, y) pixels in each frame, with the middle button held.
void HeldMoves(CameraLoopHarness& harness, float x, float y, int frames, float deltaTime, LockWatch& watch)
{
	auto& game = harness.game;
	for (int frame = 0; frame < frames; ++frame)
	{
		game.mouse.x += x;
		game.mouse.y += y;
		watch.userX += x;
		harness.Frame(deltaTime);
		watch.Add(harness);
	}
}

/// Press the middle button with no move.
void PressMiddle(CameraLoopHarness& harness, float deltaTime, LockWatch& watch)
{
	harness.game.buttons[2] = true;
	harness.Frame(deltaTime);
	watch.Add(harness);
}

struct Motion
{
	const char* name;
	float x; ///< Pixels for each frame at 60 frames for each second.
	float y;
};

constexpr std::array k_Motions {Motion {"pitch", 0.0f, -4.0f}, Motion {"turn", 5.0f, 0.0f}, Motion {"both", 4.0f, -3.0f}};

} // namespace

TEST(HandRigCursorLock, CursorAndHandStayOnThePivotDuringAMiddleDrag)
{
	for (const auto world : k_Worlds)
	{
		for (const auto stage : k_Stages)
		{
			for (const auto rate : k_Rates)
			{
				for (const auto cursor : k_Cursors)
				{
					for (const auto& motion : k_Motions)
					{
						SCOPED_TRACE(Describe(world, stage, rate, cursor) + ", " + motion.name);
						const auto deltaTime = 1.0f / static_cast<float>(rate);
						auto harness = Settled(world, stage, cursor, deltaTime);
						auto& game = harness.game;
						game.turnDegreesPerPixel = k_TurnDegreesPerPixel;
						LockWatch watch;
						PressMiddle(harness, deltaTime, watch);
						const auto startYaw = game.direction;
						// The same moves of the mouse for each second at each frame rate.
						const auto scale = 60.0f * deltaTime;
						const auto frames = static_cast<int>(0.6f / deltaTime);
						HeldMoves(harness, motion.x * scale, motion.y * scale, frames, deltaTime, watch);
						// One pivot for the hold. The cursor and the hand stay on it.
						EXPECT_EQ(watch.pivots, 1);
						EXPECT_LE(watch.largestCursorPixels, k_PivotPixels);
						EXPECT_LE(watch.largestHandPixels, k_PivotPixels);
						// The game turns by the moves of the user, as with no lock.
						if (motion.x != 0.0f)
						{
							const auto turn = WrapDegrees(game.direction - startYaw);
							EXPECT_NEAR(turn, watch.userX * k_TurnDegreesPerPixel, 0.1f);
						}
						// At the release, the hand continues from the pivot. It moves only by the move of the user.
						const auto before = game.handScreen;
						game.buttons[2] = false;
						game.mouse.x += motion.x * scale;
						game.mouse.y += motion.y * scale;
						harness.Frame(deltaTime);
						EXPECT_LE(std::hypot(game.handScreen.x - before.x, game.handScreen.y - before.y),
						          std::hypot(motion.x, motion.y) * scale + k_PivotPixels);
						EXPECT_TRUE(game.errors.empty());
					}
				}
			}
		}
	}
}

TEST(HandRigCursorLock, WheelKeysAndSmoothingDuringAHoldKeepOnePivot)
{
	for (const auto world : {World::Flat, World::Hills})
	{
		for (const auto rate : k_Rates)
		{
			SCOPED_TRACE(std::string(Name(world)) + ", " + std::to_string(rate) + " fps");
			const auto deltaTime = 1.0f / static_cast<float>(rate);
			auto harness = Settled(world, 1.0f, Cursor::Near, deltaTime);
			auto& game = harness.game;
			LockWatch watch;
			PressMiddle(harness, deltaTime, watch);
			ASSERT_TRUE(watch.pivot);
			const auto startDistance = Length(game.transform.position - *watch.pivot);
			HeldMoves(harness, 0.0f, -3.0f, 10, deltaTime, watch);
			// A wheel notch in and the keys turn, with the button held.
			game.wheel = 0.1f;
			HeldMoves(harness, 0.0f, -1.0f, 1, deltaTime, watch);
			game.wheel = 0.0f;
			HeldMoves(harness, 2.0f, 0.0f, 10, deltaTime, watch);
			game.turnPerSecond = 40.0f;
			HeldMoves(harness, 0.0f, 0.0f, static_cast<int>(0.3f / deltaTime), deltaTime, watch);
			game.turnPerSecond = 0.0f;
			// Two notches out, and the smoothing of the zoom ends with the button held.
			for (int notch = 0; notch < 2; ++notch)
			{
				game.wheel = -0.1f;
				HeldMoves(harness, 0.0f, 2.0f, 1, deltaTime, watch);
				game.wheel = 0.0f;
				HeldMoves(harness, 0.0f, 0.0f, 5, deltaTime, watch);
			}
			HeldMoves(harness, 0.0f, 0.0f, static_cast<int>(1.0f / deltaTime), deltaTime, watch);
			EXPECT_EQ(watch.pivots, 1);
			EXPECT_LE(watch.largestCursorPixels, k_PivotPixels);
			EXPECT_LE(watch.largestHandPixels, k_PivotPixels);
			// The zoom happened, about the pivot of the hold: one notch in and two out.
			EXPECT_GT(Length(game.transform.position - *watch.pivot), startDistance * 1.2f);
			game.buttons[2] = false;
			harness.Frame(deltaTime);
			EXPECT_TRUE(game.errors.empty());
		}
	}
}

TEST(HandRigCursorLock, PressTakesTheHandThatIsBehindTheCursor)
{
	// The hand follows the cursor with a delay. At the press, the pivot is the land under the hand that the user sees,
	// and the cursor goes to the hand.
	constexpr float k_DeltaTime = 1.0f / 60.0f;
	auto harness = Settled(World::Hills, 0.0f, Cursor::Near, k_DeltaTime);
	auto& game = harness.game;
	game.scriptedHand = true;
	const WorldPoint handScreen {game.mouse.x - 20.0f, game.mouse.y - 10.0f, 0.0f};
	auto hand = game.LandAt(game.transform, handScreen);
	ASSERT_TRUE(hand);
	game.handPoint = *hand;
	LockWatch watch;
	PressMiddle(harness, k_DeltaTime, watch);
	ASSERT_TRUE(watch.pivot);
	EXPECT_LT(Length(*watch.pivot - *hand), 0.05f);
	EXPECT_LE(PixelsTo(harness, game.mouse, *hand), k_PivotPixels);
	HeldMoves(harness, 3.0f, -3.0f, 30, k_DeltaTime, watch);
	EXPECT_EQ(watch.pivots, 1);
	EXPECT_LE(watch.largestCursorPixels, k_PivotPixels);
}

TEST(HandRigCursorLock, TheCursorStaysFreeWhenTheLockIsOffOrFails)
{
	constexpr float k_DeltaTime = 1.0f / 60.0f;
	for (const auto fails : {false, true})
	{
		SCOPED_TRACE(fails ? "the system does not move the cursor" : "the lock is off");
		auto settings = HandSettings();
		settings.lockCursor = fails;
		auto harness = Settled(World::Flat, 1.0f, Cursor::Near, k_DeltaTime, settings);
		auto& game = harness.game;
		game.warpFails = fails;
		LockWatch watch;
		PressMiddle(harness, k_DeltaTime, watch);
		const auto start = game.mouse;
		HeldMoves(harness, 0.0f, -4.0f, 20, k_DeltaTime, watch);
		// The cursor and the hand go with the mouse, and the hold still keeps one pivot.
		EXPECT_EQ(game.warps, 0);
		EXPECT_NEAR(game.mouse.y, start.y - 80.0f, 1e-3f);
		EXPECT_NEAR(game.handScreen.y, game.mouse.y, 1e-3f);
		EXPECT_EQ(watch.pivots, 1);
		EXPECT_TRUE(game.errors.empty());
	}
}

TEST(HandRigPickup, APickupAtDetailDoesNotZoom)
{
	// While the hand holds an object, the game alone keeps the zoom stage at 0.5 or more, so a pickup at Detail zooms
	// out. With the mod on, the game does not keep that smallest stage: the stage and the camera do not change. The
	// mod puts the value of the game back after each frame.
	FakeCameraGame alone;
	alone.Start({}, 0.0f);
	alone.holdsObject = true;
	for (int frame = 0; frame < 120; ++frame)
	{
		alone.RunFrame(1.0f / 60.0f);
		alone.EndFrame();
	}
	EXPECT_NEAR(alone.stage, FakeCameraGame::k_HoldMinimumStage, 1e-4f);

	for (const auto mode : {PitchMode::Hand, PitchMode::InPlace})
	{
		for (const auto rate : k_Rates)
		{
			SCOPED_TRACE(std::string(PitchModeName(mode)) + ", " + std::to_string(rate) + " fps");
			const auto deltaTime = 1.0f / static_cast<float>(rate);
			auto settings = HandSettings();
			settings.pitchMode = mode;
			auto harness = Settled(World::Flat, 0.0f, Cursor::Near, deltaTime, settings);
			auto& game = harness.game;
			const auto start = game.transform.position;
			game.holdsObject = true;
			float largestMove = 0.0f;
			for (float time = 0.0f; time < 2.0f; time += deltaTime)
			{
				harness.Frame(deltaTime);
				largestMove = std::max(largestMove, Length(game.transform.position - start));
				EXPECT_NEAR(game.heldObjectMinimumStage, FakeCameraGame::k_HoldMinimumStage, 1e-6f);
			}
			EXPECT_NEAR(game.stage, 0.0f, 1e-4f);
			EXPECT_LT(largestMove, 1e-3f);
			game.holdsObject = false;
			for (float time = 0.0f; time < 1.0f; time += deltaTime)
			{
				harness.Frame(deltaTime);
			}
			EXPECT_NEAR(game.stage, 0.0f, 1e-4f);
			EXPECT_LT(Length(game.transform.position - start), 1e-3f);
			EXPECT_TRUE(game.errors.empty());
		}
	}
}

TEST(HandRigPickup, TheWheelZoomsInToDetailWhileTheHandHoldsAnObject)
{
	// The game alone does not zoom in below stage 0.5 with an object in the hand. With the mod on, the wheel zooms in
	// to Detail.
	constexpr float k_DeltaTime = 1.0f / 60.0f;
	auto harness = Settled(World::Flat, 1.0f, Cursor::Near, k_DeltaTime);
	auto& game = harness.game;
	game.holdsObject = true;
	harness.Frame(k_DeltaTime);
	game.wheel = 0.2f;
	harness.Frame(k_DeltaTime);
	game.wheel = 0.0f;
	for (float time = 0.0f; time < 3.0f; time += k_DeltaTime)
	{
		harness.Frame(k_DeltaTime);
	}
	EXPECT_NEAR(game.stage, 0.0f, 1e-3f);
	EXPECT_TRUE(game.errors.empty());
}

TEST(HandRigPickup, ATakeAboveTheSmallestStageDoesNotMoveTheCamera)
{
	for (const auto stage : {0.5f, 1.0f, 2.0f})
	{
		SCOPED_TRACE(std::to_string(stage));
		constexpr float k_DeltaTime = 1.0f / 60.0f;
		auto harness = Settled(World::Hills, stage, Cursor::Near, k_DeltaTime);
		auto& game = harness.game;
		const auto start = game.transform.position;
		game.holdsObject = true;
		for (float time = 0.0f; time < 1.0f; time += k_DeltaTime)
		{
			harness.Frame(k_DeltaTime);
		}
		EXPECT_LT(Length(game.transform.position - start), 1e-3f);
		game.holdsObject = false;
		for (float time = 0.0f; time < 1.0f; time += k_DeltaTime)
		{
			harness.Frame(k_DeltaTime);
		}
		EXPECT_LT(Length(game.transform.position - start), 1e-3f);
	}
}

TEST(HandRigPickup, ThePivotIsOnTheLandWhileTheHandHoldsAnObject)
{
	// The hand point of the game is on the object that the hand holds, near the camera. The pivot of a middle drag
	// is the land under the cursor, never the object.
	constexpr float k_DeltaTime = 1.0f / 60.0f;
	auto harness = Settled(World::Hills, 0.0f, Cursor::Near, k_DeltaTime);
	auto& game = harness.game;
	game.holdsObject = true;
	for (float time = 0.0f; time < 1.5f; time += k_DeltaTime)
	{
		harness.Frame(k_DeltaTime);
	}
	game.scriptedHand = true;
	const auto ray = ScreenRay(game.ScreenCameraOf(game.transform), {game.mouse.x, game.mouse.y});
	game.handPoint = ray.origin + ray.direction * 3.0f;
	LockWatch watch;
	PressMiddle(harness, k_DeltaTime, watch);
	ASSERT_TRUE(watch.pivot);
	EXPECT_LT(std::abs(watch.pivot->y - game.GroundAt(watch.pivot->x, watch.pivot->z)), 0.05f);
	HeldMoves(harness, 2.0f, -3.0f, 30, k_DeltaTime, watch);
	EXPECT_EQ(watch.pivots, 1);
	EXPECT_LE(watch.largestCursorPixels, k_PivotPixels);
}

TEST(HandRigReplay, Run6HoldsKeepOnePivotAndTheCursorOnIt)
{
	// Middle drags of run 6 (2026-10-19, the hand rig of main 93590d6). In the run, a wheel notch with the button
	// held took a new pivot at the cursor, and the hand followed the mouse away from the pivot. The replay plays the
	// moves of the mouse, the buttons, the wheel and the turns of the run.
	for (const auto* file : {"camera-trace-run6-held-wheel.csv", "camera-trace-run6-held-pitch.csv"})
	{
		SCOPED_TRACE(file);
		const auto frames = ReadTraceFrames(std::string(MOA_TEST_DATA_DIR) + "/" + file);
		ASSERT_GT(frames.size(), 50U);
		// The re-takes of the pivot in the run: a new pivot in a frame where the button stayed held.
		int recorded = 0;
		for (size_t index = 1; index < frames.size(); ++index)
		{
			const auto& a = frames[index - 1];
			const auto& b = frames[index];
			recorded += a.middle && b.middle && !SamePoint(a.oldPivot, b.oldPivot) ? 1 : 0;
		}

		CameraLoopHarness harness(Run5Settings(PitchMode::Hand));
		auto& game = harness.game;
		game.screenWidth = 3840.0f;
		game.screenHeight = 1440.0f;
		game.focusHeightEase = true;
		const auto& first = frames.front();
		const auto level = first.focus.y;
		game.ground = [level](float, float) { return level; };
		game.direction = first.direction;
		game.StartAt(first.focus, first.stage, {.height = first.height, .tilt = first.tilt});
		game.mouse = first.mouse;
		harness.Frame(first.deltaTime);

		int retakes = 0;
		int heldFrames = 0;
		float largestCursor = 0.0f;
		float largestHand = 0.0f;
		std::optional<WorldPoint> pivot;
		auto lastMouse = first.mouse;
		for (const auto& frame : frames)
		{
			game.buttons[2] = frame.middle;
			game.buttons[1] = frame.right;
			game.mouse = game.mouse + (frame.mouse - lastMouse);
			lastMouse = frame.mouse;
			game.wheel = frame.wheel;
			game.turnPerSecond = WrapDegrees(frame.gameDirection - frame.direction) / frame.deltaTime;
			harness.Frame(frame.deltaTime);
			const auto& trace = harness.loop.Trace();
			if (!frame.middle || !trace.pivot)
			{
				pivot.reset();
				continue;
			}
			++heldFrames;
			if (pivot && !SamePoint(*pivot, *trace.pivot))
			{
				++retakes;
			}
			pivot = trace.pivot;
			largestCursor = std::max(largestCursor, PixelsTo(harness, game.mouse, *pivot));
			largestHand = std::max(largestHand, PixelsTo(harness, game.handScreen, *pivot));
		}
		std::cout << file << ": pivot re-takes with the button held: run " << recorded << ", replay " << retakes
		          << "; largest distance to the pivot: cursor " << largestCursor << " px, hand " << largestHand << " px in "
		          << heldFrames << " frames" << std::endl;
		EXPECT_GT(recorded, 0);
		EXPECT_EQ(retakes, 0);
		EXPECT_GT(heldFrames, 20);
		EXPECT_LE(largestCursor, k_PivotPixels);
		EXPECT_LE(largestHand, k_PivotPixels);
		EXPECT_TRUE(game.errors.empty());
	}
}

namespace
{

/// What a replay of the middle drags of run 7 did to the camera.
struct FarPivotResult
{
	float shownPath = 0.0f;     ///< Metres.
	float largestExcess = 0.0f; ///< The largest height of the camera above the land, divided by the stage height.
	float endExcess = 0.0f;     ///< The same ratio at the end of the replay.
	float endStage = 0.0f;
	float largestPivotGrowth = 0.0f; ///< The largest distance to the pivot in a hold, divided by the distance at the press.
};

/// Replay frames of run 7 with the cursor lock: the move of the mouse in each frame is the move from the place where
/// the mod put the cursor. Before the replay, a vertical middle drag gives the view the raise of the first frame.
[[nodiscard]] FarPivotResult ReplayFarPivot(const std::vector<TraceFrame>& frames)
{
	const auto settings = Run5Settings(PitchMode::Hand);
	CameraLoopHarness harness(settings);
	auto& game = harness.game;
	game.screenWidth = 3840.0f;
	game.screenHeight = 1440.0f;
	game.focusHeightEase = true;
	const auto& first = frames.front();
	const auto level = first.focus.y;
	game.ground = [level](float, float) { return level; };
	game.direction = first.direction;
	game.mouse = first.mouse;
	// In the run, a wheel notch before the first frame zoomed out to stage 2, and the zoom still eased in the first
	// frames. The replay starts half a stage lower and makes the same notch after the raise.
	const auto zoomStart = first.stage < 0.5f ? 0.0f : std::round(first.stage * 2.0f) * 0.5f - 0.5f;
	game.Start(first.focus, zoomStart);
	constexpr float k_DeltaTime = 1.0f / 60.0f;
	harness.Frame(k_DeltaTime);
	game.buttons[2] = true;
	harness.Frame(k_DeltaTime);
	const auto pixels = first.raise * 100.0f / static_cast<float>(settings.pitchSensitivity);
	for (int frame = 0; frame < 20; ++frame)
	{
		game.mouse.y += pixels / 20.0f;
		harness.Frame(k_DeltaTime);
	}
	game.buttons[2] = false;
	for (float time = 0.0f; time < 0.5f; time += k_DeltaTime)
	{
		harness.Frame(k_DeltaTime);
	}
	if (zoomStart < first.stage)
	{
		game.wheel = -0.1f;
		harness.Frame(k_DeltaTime);
		game.wheel = 0.0f;
		for (int frame = 0; frame < 120 && game.stage < first.stage; ++frame)
		{
			harness.Frame(k_DeltaTime);
		}
	}

	std::vector<StageView> table;
	for (const auto& row : FakeCameraGame::k_StageTable)
	{
		table.push_back({.height = row.height, .tilt = row.tilt});
	}
	FarPivotResult result;
	std::optional<float> pressDistance;
	for (const auto& frame : frames)
	{
		game.buttons[2] = frame.middle;
		game.buttons[1] = frame.right;
		game.mouse = game.mouse + frame.mouseDelta;
		game.mouse.x = std::clamp(game.mouse.x, 0.0f, game.screenWidth);
		game.mouse.y = std::clamp(game.mouse.y, 0.0f, game.screenHeight);
		game.wheel = frame.wheel;
		game.turnPerSecond = WrapDegrees(frame.gameDirection - frame.direction) / frame.deltaTime;
		harness.Frame(frame.deltaTime);
		result.shownPath += harness.ShownMove();
		const auto excess = (game.transform.position.y - level) / InterpolateStage(table, game.stage).height;
		result.largestExcess = std::max(result.largestExcess, excess);
		result.endExcess = excess;
		result.endStage = game.stage;
		const auto& pivot = harness.loop.Trace().pivot;
		if (!frame.middle || !pivot)
		{
			pressDistance.reset();
			continue;
		}
		const auto distance = Length(game.transform.position - *pivot);
		if (!pressDistance)
		{
			pressDistance = distance;
		}
		result.largestPivotGrowth = std::max(result.largestPivotGrowth, distance / *pressDistance);
	}
	return result;
}

} // namespace

TEST(HandRigReplay, Run7ZoomDoesNotThrowTheCamera)
{
	// Middle drags 1 and 2 of run 7 (2026-10-19, main 4a2bc18). In drag 1 the zoom still eased from stage 1.84 to 2
	// while the user made the view flat. The zoom kept the height above the pivot, so the distance to the pivot grew
	// from 187 to 637 metres in 40 frames, and the steep pitch after that moved the camera 400 metres up. The zoom in
	// kept the height as a ratio of the stage height: at Detail the camera was 37 metres above the land (six times the
	// stage height), and the zoom out after drag 2 took it to 3,677 metres at stage 3.4. The replay plays the moves of
	// the mouse, the buttons, the wheel and the turns of the run.
	const auto frames = ReadTraceFrames(std::string(MOA_TEST_DATA_DIR) + "/camera-trace-run7-far-pivot.csv");
	ASSERT_GT(frames.size(), 300U);
	const auto result = ReplayFarPivot(frames);
	std::cout << "run 7 replay: shown path " << result.shownPath << " m, largest height / stage " << result.largestExcess
	          << ", at the end " << result.endExcess << " (stage " << result.endStage
	          << "), largest growth of the distance to the pivot in a hold " << result.largestPivotGrowth << std::endl;
	EXPECT_GT(result.endStage, 3.0f);
	EXPECT_LT(result.endExcess, 1.5f);
	EXPECT_LT(result.largestExcess, 3.0f);
	EXPECT_LT(result.largestPivotGrowth, 1.5f);
}

TEST(HandRigPivotLimit, AFarHandGivesANearerPivotOnTheSameLine)
{
	// At Detail, a press with the hand near the horizon takes land about 200 metres away. A pitch about that land
	// moves the camera far. With the limit, the pivot is at three times the camera distance of the stage, on the line to
	// the hand: it stays at the place of the cursor on the screen, and the camera moves much less.
	constexpr float k_DeltaTime = 1.0f / 60.0f;
	struct Result
	{
		float move = 0.0f;
		float pivotDistance = 0.0f;
		bool limited = false;
		LockWatch watch;
	};
	auto run = [&](double limit) {
		auto settings = HandSettings();
		settings.maxPivotDistance = limit;
		auto harness = Settled(World::Flat, 0.0f, Cursor::Near, k_DeltaTime, settings);
		auto& game = harness.game;
		game.mouse = {960.0f, 850.0f, 0.0f};
		for (int frame = 0; frame < 30; ++frame)
		{
			harness.Frame(k_DeltaTime);
		}
		Result result;
		const auto start = game.transform.position;
		PressMiddle(harness, k_DeltaTime, result.watch);
		result.limited = harness.loop.Trace().pivotLimited;
		if (const auto& pivot = harness.loop.Trace().pivot)
		{
			result.pivotDistance = Length(*pivot - game.transform.position);
		}
		HeldMoves(harness, 0.0f, -4.0f, 20, k_DeltaTime, result.watch);
		result.move = Length(game.transform.position - start);
		EXPECT_TRUE(game.errors.empty());
		return result;
	};
	const auto free = run(0.0);
	const auto limited = run(3.0);
	std::cout << "far hand at Detail: pivot " << free.pivotDistance << " m, camera move " << free.move
	          << " m; with the limit: pivot " << limited.pivotDistance << " m, camera move " << limited.move << " m"
	          << std::endl;
	// The camera distance of Detail is 6 / sin(20 degrees) = 17.5 metres.
	const auto limit = 3.0f * 6.0f / std::sin(20.0f * k_DegreesToRadians);
	EXPECT_FALSE(free.limited);
	EXPECT_GT(free.pivotDistance, 2.0f * limit);
	EXPECT_TRUE(limited.limited);
	EXPECT_NEAR(limited.pivotDistance, limit, 0.5f);
	EXPECT_LT(limited.move, free.move * 0.5f);
	// One pivot, and the cursor stays on it.
	EXPECT_EQ(limited.watch.pivots, 1);
	EXPECT_LE(limited.watch.largestCursorPixels, k_PivotPixels);
}
