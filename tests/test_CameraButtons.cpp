/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <cmath>

#include <algorithm>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CameraLoopHarness.h"

using namespace moa_mod;
using namespace moa_mod::camera;
using moa_mod::camera::test::CameraLoopHarness;
using moa_mod::camera::test::ReadTraceFrames;
using moa_mod::camera::test::TraceFrame;

namespace
{

/// The turn of the game for each pixel of a horizontal middle button drag in run 11, degrees.
constexpr float k_Run11TurnDegreesPerPixel = 0.11f;

/// The settings of run 11: the hand rig with the pitch, the look up and the spring arm.
[[nodiscard]] moa_mod::camera::CameraSettings Run11Settings()
{
	moa_mod::camera::CameraSettings settings;
	settings.enabled = true;
	settings.pitch = true;
	settings.lookUp = true;
	settings.pitchMode = PitchMode::Hand;
	return settings;
}

/// What the loop did while the user held the middle button.
struct HoldWatch
{
	int holds = 0;
	int heldFrames = 0;
	int framesNotSeen = 0;             ///< Held frames in which the loop did not see the middle button.
	int framesWithNoLock = 0;          ///< Held frames after the press in which the loop did not keep the cursor.
	int pivotsTakenAgain = 0;          ///< New pivots in a hold, after the press.
	int newPivotFrames = 0;            ///< Held frames after the press in which the desired distance changed for a new pivot.
	float largestDesiredChange = 0.0f; ///< The largest change of the desired distance in a hold with no zoom, a part of it.
	float raiseChange = 0.0f;          ///< The largest change of the raise in a hold, degrees.

	std::optional<WorldPoint> pivot;
	std::optional<float> desired;
	float pressRaise = 0.0f;
	bool wasHeld = false;

	void Add(const CameraLoopHarness& harness, bool held)
	{
		const auto& trace = harness.loop.Trace();
		const auto pressed = held && !wasHeld;
		wasHeld = held;
		if (!held)
		{
			return;
		}
		++heldFrames;
		if (!trace.modMiddle || !*trace.modMiddle)
		{
			++framesNotSeen;
		}
		if (pressed)
		{
			++holds;
			pivot = trace.pivot;
			desired = trace.desiredDistance;
			pressRaise = trace.raise;
			return;
		}
		if (!trace.cursorLock)
		{
			++framesWithNoLock;
		}
		// A move of the game moves the pivot with the camera. Any other change of the pivot is a new pivot.
		if (trace.pivot && (!pivot || Length(*pivot + trace.gameShift - *trace.pivot) > 1e-3f))
		{
			++pivotsTakenAgain;
		}
		pivot = trace.pivot;
		if (trace.desiredChange == 2)
		{
			++newPivotFrames;
		}
		if (trace.desiredChange == 1)
		{
			// A zoom changes the desired distance.
			desired = trace.desiredDistance;
		}
		else if (desired && trace.desiredDistance && *desired > 0.0f)
		{
			largestDesiredChange = std::max(largestDesiredChange, std::abs(*trace.desiredDistance - *desired) / *desired);
		}
		raiseChange = std::max(raiseChange, std::abs(trace.raise - pressRaise));
	}
};

/// Play a part of a trace of run 11 in the model of the game, with the mouse button of the game as in that run: it
/// gives no middle button (Steam Input), but the game turns with the middle button.
[[nodiscard]] HoldWatch ReplayRun11(const std::vector<TraceFrame>& frames)
{
	CameraLoopHarness harness(Run11Settings());
	auto& game = harness.game;
	game.screenWidth = 3840.0f;
	game.screenHeight = 1440.0f;
	game.focusHeightEase = true;
	game.gameButtonWithoutMiddle = true;
	game.turnDegreesPerPixel = k_Run11TurnDegreesPerPixel;
	const auto& first = frames.front();
	const auto level = first.focus.y;
	game.ground = [level](float, float) { return level; };
	game.direction = first.direction;
	game.mouse = first.mouse;
	game.Start(first.focus, first.stage);
	constexpr float k_DeltaTime = 1.0f / 60.0f;
	for (float time = 0.0f; time < 1.0f; time += k_DeltaTime)
	{
		harness.Frame(k_DeltaTime);
	}

	HoldWatch watch;
	for (const auto& frame : frames)
	{
		game.buttons[2] = frame.middle;
		game.buttons[1] = frame.right;
		game.mouse = game.mouse + frame.mouseDelta;
		game.mouse.x = std::clamp(game.mouse.x, 0.0f, game.screenWidth);
		game.mouse.y = std::clamp(game.mouse.y, 0.0f, game.screenHeight);
		game.wheel = frame.wheel;
		harness.Frame(frame.deltaTime);
		watch.Add(harness, frame.middle);
	}
	EXPECT_TRUE(game.errors.empty());
	return watch;
}

void Print(const char* name, const HoldWatch& watch)
{
	std::cout << name << ": " << watch.holds << " holds, " << watch.heldFrames << " held frames, " << watch.framesNotSeen
	          << " not seen, " << watch.framesWithNoLock << " with no cursor lock, " << watch.pivotsTakenAgain
	          << " pivots taken again, " << watch.newPivotFrames
	          << " frames with a new pivot, largest change of the desired distance " << watch.largestDesiredChange * 100.0f
	          << " %, largest change of the raise " << watch.raiseChange << " deg" << std::endl;
}

/// The two kinds of a button source of the game that is not the mouse button of Unity.
enum class SourceKind
{
	GameButtonWithoutMiddle, ///< The middle button of the mouse is held, but the mouse button of the game does not give it.
	TurnKey,                 ///< The middle button of the mouse is up, but the user holds the key of the turn.
};

} // namespace

TEST(MouseButtons, TheMiddleButtonComesFromTheTurnInputOfTheGame)
{
	const ButtonSources all {.turnInput = true, .gameButton = true};
	EXPECT_EQ(ButtonSourceFor(k_MiddleButton, all), ButtonSource::TurnInput);
	EXPECT_EQ(ButtonSourceFor(k_LeftButton, all), ButtonSource::GameButton);
	EXPECT_EQ(ButtonSourceFor(k_RightButton, all), ButtonSource::GameButton);
	// The mouse button of the game is never the source of the middle button.
	EXPECT_EQ(ButtonSourceFor(k_MiddleButton, {.turnInput = false, .gameButton = true}), ButtonSource::Unity);
	EXPECT_EQ(ButtonSourceFor(k_LeftButton, {.turnInput = true, .gameButton = false}), ButtonSource::Unity);
	EXPECT_EQ(DescribeButtonSources(all),
	          "left and right button from the mouse button of the game, middle button from the turn input of the game");
}

TEST(MouseButtons, TheModAgreesWithTheTurnOfTheGameForEachSource)
{
	// The mouse button of the game can differ from the mouse button of Unity. With Steam Input the mouse button of the
	// game never gives the middle button, but the game still turns with it (run 11). With a key that the user gave to
	// the turn, the game turns with the middle button of the mouse up. The mod must see a hold when the game turns:
	// one pivot, the cursor on the pivot, and the vertical drag pitches.
	constexpr float k_DeltaTime = 1.0f / 60.0f;
	for (const auto kind : {SourceKind::GameButtonWithoutMiddle, SourceKind::TurnKey})
	{
		for (const auto turnInput : {true, false})
		{
			if (kind == SourceKind::TurnKey && !turnInput)
			{
				// With no turn input, the mod cannot see a key of the turn.
				continue;
			}
			SCOPED_TRACE(std::string(kind == SourceKind::TurnKey ? "turn key" : "game button without middle") +
			             (turnInput ? "" : ", no turn input"));
			CameraLoopHarness harness(Run11Settings());
			auto& game = harness.game;
			game.focusHeightEase = true;
			game.turnDegreesPerPixel = k_Run11TurnDegreesPerPixel;
			game.gameButtonWithoutMiddle = true;
			game.turnInputAvailable = turnInput;
			game.mouse = {1100.0f, 300.0f, 0.0f};
			game.Start({}, 1.0f);
			for (float time = 0.0f; time < 1.0f; time += k_DeltaTime)
			{
				harness.Frame(k_DeltaTime);
			}
			const auto press = [&](bool held) {
				if (kind == SourceKind::TurnKey)
				{
					game.turnKeyHeld = held;
				}
				else
				{
					game.buttons[2] = held;
				}
			};
			HoldWatch watch;
			press(true);
			harness.Frame(k_DeltaTime);
			watch.Add(harness, true);
			const auto startDirection = game.direction;
			for (int frame = 0; frame < 40; ++frame)
			{
				game.mouse.x += 4.0f;
				game.mouse.y += frame < 20 ? -5.0f : 0.0f;
				harness.Frame(k_DeltaTime);
				watch.Add(harness, true);
			}
			press(false);
			harness.Frame(k_DeltaTime);
			Print("source", watch);
			EXPECT_EQ(watch.framesNotSeen, 0);
			EXPECT_EQ(watch.framesWithNoLock, 0);
			EXPECT_EQ(watch.pivotsTakenAgain, 0);
			EXPECT_EQ(watch.newPivotFrames, 0);
			EXPECT_LT(watch.largestDesiredChange, 0.01f);
			// 100 pixels down at 10 degrees for each 100 pixels.
			EXPECT_GT(watch.raiseChange, 5.0f);
			// The game turned by the moves of the user.
			EXPECT_GT(std::abs(WrapDegrees(game.direction - startDirection)), 5.0f);
			EXPECT_TRUE(game.errors.empty());
		}
	}
}

TEST(MouseButtons, TheCursorLockTellsOnceWhyItIsOff)
{
	constexpr float k_DeltaTime = 1.0f / 60.0f;
	CameraLoopHarness harness(Run11Settings());
	auto& game = harness.game;
	game.warpFails = true;
	game.mouse = {1100.0f, 300.0f, 0.0f};
	game.Start({}, 1.0f);
	for (float time = 0.0f; time < 1.0f; time += k_DeltaTime)
	{
		harness.Frame(k_DeltaTime);
	}
	for (int hold = 0; hold < 2; ++hold)
	{
		game.buttons[2] = true;
		for (int frame = 0; frame < 10; ++frame)
		{
			game.mouse.y -= 3.0f;
			harness.Frame(k_DeltaTime);
			ASSERT_TRUE(harness.loop.Trace().cursorLockOff.has_value());
			EXPECT_EQ(*harness.loop.Trace().cursorLockOff, static_cast<int>(CursorLockOff::WarpFailed));
		}
		game.buttons[2] = false;
		harness.Frame(k_DeltaTime);
		EXPECT_FALSE(harness.loop.Trace().cursorLockOff.has_value());
	}
	const auto told = std::ranges::count_if(game.debugLines, [](const std::string& line) {
		return line.find("does not keep the cursor on the pivot") != std::string::npos;
	});
	EXPECT_EQ(told, 1);
}

TEST(HandRigReplay, Run11MiddleDragsKeepOnePivotAndPitch)
{
	// Run 11 (2026-10-19, main d17b4b1). The mod read the middle button through the mouse button of the game, which
	// never gives it with Steam Input. So the mod saw no hold: no cursor lock and no pitch, and each part of a turn took
	// a new pivot and a new desired distance of the spring arm. The replay plays the moves of the mouse, the buttons and
	// the wheel of a vertical drag (frames 2246 to 2265, 385 pixels down) and of a turn to the right and back (frames
	// 5162 to 5446).
	const auto pitch = ReplayRun11(ReadTraceFrames(std::string(MOA_TEST_DATA_DIR) + "/camera-trace-run11-pitch.csv"));
	Print("run 11 pitch replay", pitch);
	EXPECT_EQ(pitch.holds, 1);
	EXPECT_EQ(pitch.framesNotSeen, 0);
	EXPECT_EQ(pitch.framesWithNoLock, 0);
	EXPECT_EQ(pitch.pivotsTakenAgain, 0);
	EXPECT_EQ(pitch.newPivotFrames, 0);
	EXPECT_LT(pitch.largestDesiredChange, 0.01f);
	// 385 pixels down at 10 degrees for each 100 pixels, less the limit of the pitch.
	EXPECT_GT(pitch.raiseChange, 10.0f);

	const auto turn = ReplayRun11(ReadTraceFrames(std::string(MOA_TEST_DATA_DIR) + "/camera-trace-run11-turn.csv"));
	Print("run 11 turn replay", turn);
	EXPECT_GE(turn.holds, 1);
	EXPECT_EQ(turn.framesNotSeen, 0);
	EXPECT_EQ(turn.framesWithNoLock, 0);
	EXPECT_EQ(turn.pivotsTakenAgain, 0);
	EXPECT_EQ(turn.newPivotFrames, 0);
	EXPECT_LT(turn.largestDesiredChange, 0.01f);
}

TEST(MouseButtons, AMoveOfTheGameDuringAHoldKeepsThePivotAndTheDesiredDistance)
{
	// A key pan, and the glide and the fade of a land drag that the user just released, move the camera of the game
	// while the user holds the middle button. The pivot moves with the camera, so the spring arm keeps its pivot and
	// its desired distance.
	constexpr float k_DeltaTime = 1.0f / 60.0f;
	for (const auto glide : {false, true})
	{
		SCOPED_TRACE(glide ? "a press after a land drag" : "a key pan during the hold");
		CameraLoopHarness harness(Run11Settings());
		auto& game = harness.game;
		game.focusHeightEase = true;
		game.turnDegreesPerPixel = k_Run11TurnDegreesPerPixel;
		game.gameButtonWithoutMiddle = true;
		game.mouse = {1100.0f, 300.0f, 0.0f};
		game.Start({}, 1.0f);
		for (float time = 0.0f; time < 1.0f; time += k_DeltaTime)
		{
			harness.Frame(k_DeltaTime);
		}
		if (glide)
		{
			// A fast land drag, then the release and at once the press of the middle button.
			game.buttons[1] = true;
			for (int frame = 0; frame < 20; ++frame)
			{
				game.mouse.x -= 15.0f;
				harness.Frame(k_DeltaTime);
			}
			game.buttons[1] = false;
			harness.Frame(k_DeltaTime);
		}
		HoldWatch watch;
		game.buttons[2] = true;
		harness.Frame(k_DeltaTime);
		watch.Add(harness, true);
		game.pan = glide ? WorldPoint {} : WorldPoint {6.0f, 0.0f, 3.0f};
		const auto start = game.focus;
		for (int frame = 0; frame < 60; ++frame)
		{
			game.mouse.x += 3.0f;
			harness.Frame(k_DeltaTime);
			watch.Add(harness, true);
		}
		game.pan = {};
		game.buttons[2] = false;
		harness.Frame(k_DeltaTime);
		Print(glide ? "press after a land drag" : "key pan in a hold", watch);
		if (!glide)
		{
			EXPECT_GT(Length(game.focus - start), 3.0f);
		}
		EXPECT_EQ(watch.framesWithNoLock, 0);
		EXPECT_EQ(watch.pivotsTakenAgain, 0);
		EXPECT_EQ(watch.newPivotFrames, 0);
		EXPECT_LT(watch.largestDesiredChange, 0.01f);
		EXPECT_TRUE(game.errors.empty());
	}
}
