/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <cmath>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CameraLoopHarness.h"

using namespace moa_mod;
using namespace moa_mod::camera;
using moa_mod::camera::test::CameraLoopHarness;

// The look up of the hand rig to a view straight up, and back.

namespace
{

constexpr std::array k_Rates {30, 60, 144};
constexpr std::array k_Stages {0.0f, 2.0f, 4.0f};
/// The pitch of the camera for the highest look up, degrees below the horizon.
constexpr float k_StraightUp = -k_HighestLookUpDegrees;
/// The vertical move of the mouse for each second of a drag, pixels. With the sensitivity of 10 degrees for each 100
/// pixels, this is 100 degrees for each second.
constexpr float k_DragPixelsPerSecond = 1000.0f;

/// The settings of the user: the hand rig, with the tilt from 1 to 89 degrees and the default look up.
[[nodiscard]] CameraSettings LookUpSettings()
{
	CameraSettings settings;
	settings.enabled = true;
	settings.pitch = true;
	settings.lookUp = true;
	settings.minTiltDegrees = 1.0;
	settings.maxTiltDegrees = 89.0;
	settings.pitchSensitivity = 10.0;
	settings.pitchSmoothing = 0.1;
	settings.pitchMode = PitchMode::Hand;
	return settings;
}

/// Return the heading of a camera from its right side. It stays defined when the view is straight up.
[[nodiscard]] float HeadingOf(Quaternion rotation)
{
	const auto right = Rotate(rotation, WorldPoint {1.0f, 0.0f, 0.0f});
	return std::atan2(-right.z, right.x) / k_DegreesToRadians;
}

/// What the frames of a test did to the camera that the user sees.
struct Watch
{
	std::vector<float> moves;         ///< The move of the camera in each frame, metres.
	float largestPitchStep = 0.0f;    ///< Degrees.
	float largestRoll = 0.0f;         ///< Degrees.
	float largestHeadingError = 0.0f; ///< The heading of the camera less the heading of the game, degrees.
	float lowest = 1e9f;              ///< The lowest height of the camera above the land, metres.
	float lastPitch = 0.0f;

	void Start(const CameraLoopHarness& harness) { lastPitch = PitchOf(harness.game.transform.rotation); }

	void Add(const CameraLoopHarness& harness)
	{
		const auto& game = harness.game;
		const auto rotation = game.transform.rotation;
		const auto pitch = PitchOf(rotation);
		moves.push_back(harness.ShownMove());
		largestPitchStep = std::max(largestPitchStep, std::abs(pitch - lastPitch));
		lastPitch = pitch;
		largestRoll = std::max(largestRoll, std::abs(RollDegrees(rotation)));
		largestHeadingError = std::max(largestHeadingError, std::abs(WrapDegrees(HeadingOf(rotation) - game.direction)));
		const auto position = game.transform.position;
		lowest = std::min(lowest, position.y - game.GroundAt(position.x, position.z));
	}

	/// Return the largest move of a frame that is much larger than the moves of the frames before and after it: a jump.
	/// A move can start or stop in one frame, so only a move that is larger than both of its neighbours counts.
	[[nodiscard]] float LargestJump() const
	{
		float largest = 0.0f;
		for (size_t index = 1; index + 1 < moves.size(); ++index)
		{
			const auto neighbours = std::max(moves[index - 1], moves[index + 1]);
			if (moves[index] > 2.0f * neighbours + 0.05f)
			{
				largest = std::max(largest, moves[index]);
			}
		}
		return largest;
	}
};

void RunFor(CameraLoopHarness& harness, float seconds, float deltaTime, Watch& watch)
{
	for (float time = 0.0f; time < seconds; time += deltaTime)
	{
		harness.Frame(deltaTime);
		watch.Add(harness);
	}
}

/// Hold the middle button and move the mouse by `pixelsPerSecond` up (positive) or down for `seconds`. Then release.
void VerticalDrag(CameraLoopHarness& harness, float pixelsPerSecond, float seconds, float deltaTime, Watch& watch)
{
	auto& game = harness.game;
	game.buttons[2] = true;
	harness.Frame(deltaTime);
	watch.Add(harness);
	for (float time = 0.0f; time < seconds; time += deltaTime)
	{
		game.mouse.y += pixelsPerSecond * deltaTime;
		harness.Frame(deltaTime);
		watch.Add(harness);
	}
	game.buttons[2] = false;
}

/// A harness on hills at a stage, settled for one second.
[[nodiscard]] CameraLoopHarness Settled(float stage, float deltaTime, const CameraSettings& settings = LookUpSettings())
{
	CameraLoopHarness harness(settings);
	auto& game = harness.game;
	game.focusHeightEase = true;
	game.ground = [](float x, float z) { return 100.0f + 12.0f * std::sin(x / 37.0f) * std::cos(z / 53.0f); };
	game.mouse = {1100.0f, 300.0f, 0.0f};
	game.Start({}, stage);
	for (float time = 0.0f; time < 1.0f; time += deltaTime)
	{
		harness.Frame(deltaTime);
	}
	return harness;
}

/// Raise the view to straight up: a drag of two seconds, then one second with no input.
void RaiseToStraightUp(CameraLoopHarness& harness, float deltaTime, Watch& watch)
{
	VerticalDrag(harness, k_DragPixelsPerSecond, 2.0f, deltaTime, watch);
	RunFor(harness, 1.0f, deltaTime, watch);
}

[[nodiscard]] std::string Describe(float stage, int rate)
{
	return "stage " + std::to_string(stage) + ", " + std::to_string(rate) + " fps";
}

} // namespace

TEST(LookUp, RaisesToStraightUpAndBackWithNoJump)
{
	for (const auto stage : k_Stages)
	{
		for (const auto rate : k_Rates)
		{
			SCOPED_TRACE(Describe(stage, rate));
			const auto deltaTime = 1.0f / static_cast<float>(rate);
			auto harness = Settled(stage, deltaTime);
			auto& game = harness.game;
			const auto startPitch = PitchOf(game.transform.rotation);
			const auto startDirection = game.direction;
			Watch watch;
			watch.Start(harness);
			RaiseToStraightUp(harness, deltaTime, watch);
			EXPECT_NEAR(PitchOf(game.transform.rotation), k_StraightUp, 0.05f);
			// The view stays up with no input, and the camera does not move.
			const auto up = game.transform.position;
			RunFor(harness, 1.0f, deltaTime, watch);
			EXPECT_LT(Length(game.transform.position - up), 1e-3f);
			EXPECT_NEAR(PitchOf(game.transform.rotation), k_StraightUp, 0.05f);
			// A new drag from the sky down to the view of the start: the target of the raise stopped at the limit, so this
			// drag is the full range of the raise.
			const auto pixels = (startPitch - k_StraightUp) * 10.0f;
			VerticalDrag(harness, -k_DragPixelsPerSecond, pixels / k_DragPixelsPerSecond, deltaTime, watch);
			RunFor(harness, 1.0f, deltaTime, watch);
			// The glide after the release continues the pitch down past the view of the start.
			EXPECT_GT(PitchOf(game.transform.rotation), startPitch - 0.5f);
			// The heading does not change, and the camera never rolls or jumps.
			EXPECT_NEAR(WrapDegrees(game.direction - startDirection), 0.0f, 1e-3f);
			EXPECT_LT(watch.largestHeadingError, 0.05f);
			EXPECT_LT(watch.largestRoll, 1e-3f);
			// The drag is 100 degrees for each second; the pitch smoothing makes the steps smaller.
			EXPECT_LT(watch.largestPitchStep, 100.0f * deltaTime + 0.01f);
			EXPECT_EQ(watch.LargestJump(), 0.0f);
			EXPECT_GE(watch.lowest, 1.5f - 1e-3f);
			EXPECT_TRUE(game.errors.empty());
		}
	}
}

TEST(LookUp, TurnsWhileTheViewIsStraightUp)
{
	for (const auto stage : k_Stages)
	{
		for (const auto rate : k_Rates)
		{
			SCOPED_TRACE(Describe(stage, rate));
			const auto deltaTime = 1.0f / static_cast<float>(rate);
			auto harness = Settled(stage, deltaTime);
			auto& game = harness.game;
			Watch watch;
			watch.Start(harness);
			RaiseToStraightUp(harness, deltaTime, watch);
			const auto start = game.direction;
			const auto up = game.transform.position;
			// A new press on the sky, then a horizontal drag: the game turns 90 degrees for each second.
			game.buttons[2] = true;
			harness.Frame(deltaTime);
			watch.Add(harness);
			game.turnPerSecond = 90.0f;
			for (float time = 0.0f; time < 1.0f; time += deltaTime)
			{
				game.mouse.x += 300.0f * deltaTime;
				harness.Frame(deltaTime);
				watch.Add(harness);
			}
			game.turnPerSecond = 0.0f;
			game.buttons[2] = false;
			RunFor(harness, 1.0f, deltaTime, watch);
			// The heading turned, and the view stayed straight up.
			EXPECT_GT(std::abs(WrapDegrees(game.direction - start)), 80.0f);
			EXPECT_NEAR(PitchOf(game.transform.rotation), k_StraightUp, 0.05f);
			EXPECT_LT(watch.largestHeadingError, 0.05f);
			EXPECT_LT(watch.largestRoll, 1e-3f);
			EXPECT_EQ(watch.LargestJump(), 0.0f);
			// The camera turns about a pivot that is not farther than the limit of the pivot: three times the distance
			// of the camera of the zoom stage.
			const auto row = moa_mod::camera::test::FakeCameraGame::k_StageTable[static_cast<size_t>(stage)];
			const auto limit = 3.0f * row.height / std::sin(row.tilt * k_DegreesToRadians);
			EXPECT_LT(Length(game.transform.position - up), 2.0f * limit);
			EXPECT_GE(watch.lowest, 1.5f - 1e-3f);
			EXPECT_TRUE(game.errors.empty());
		}
	}
}

TEST(LookUp, ZoomsWhileTheViewIsUp)
{
	for (const auto rate : k_Rates)
	{
		SCOPED_TRACE(std::to_string(rate) + " fps");
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		auto harness = Settled(0.0f, deltaTime);
		auto& game = harness.game;
		Watch watch;
		watch.Start(harness);
		RaiseToStraightUp(harness, deltaTime, watch);
		// Four notches out, then four notches in. The cursor is on the sky.
		for (const auto notch : {-0.1f, -0.1f, -0.1f, -0.1f, 0.1f, 0.1f, 0.1f, 0.1f})
		{
			game.wheel = notch;
			harness.Frame(deltaTime);
			watch.Add(harness);
			game.wheel = 0.0f;
			RunFor(harness, 0.5f, deltaTime, watch);
		}
		RunFor(harness, 1.0f, deltaTime, watch);
		// The look up is permitted at each stage, so the view stays straight up.
		EXPECT_NEAR(PitchOf(game.transform.rotation), k_StraightUp, 0.05f);
		EXPECT_LT(watch.largestRoll, 1e-3f);
		EXPECT_LT(watch.largestHeadingError, 0.05f);
		EXPECT_EQ(watch.LargestJump(), 0.0f);
		EXPECT_GE(watch.lowest, 1.5f - 1e-3f);
		EXPECT_TRUE(game.errors.empty());
	}
}

TEST(LookUp, AZoomOutOfTheLookUpStagesTurnsTheViewDownSmoothly)
{
	// With the look up only near the ground (the settings of the user), a zoom out turns the view down in place.
	auto settings = LookUpSettings();
	settings.maxLookUpDegrees = k_HighestLookUpDegrees;
	settings.lookUpFullStage = 0.5;
	settings.lookUpZeroStage = 1.5;
	for (const auto rate : k_Rates)
	{
		SCOPED_TRACE(std::to_string(rate) + " fps");
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		auto harness = Settled(0.0f, deltaTime, settings);
		auto& game = harness.game;
		Watch watch;
		watch.Start(harness);
		RaiseToStraightUp(harness, deltaTime, watch);
		ASSERT_NEAR(PitchOf(game.transform.rotation), k_StraightUp, 0.05f);
		for (int notch = 0; notch < 20; ++notch)
		{
			game.wheel = -0.1f;
			harness.Frame(deltaTime);
			watch.Add(harness);
			game.wheel = 0.0f;
			RunFor(harness, 0.2f, deltaTime, watch);
		}
		RunFor(harness, 2.0f, deltaTime, watch);
		EXPECT_GT(game.stage, 1.5f);
		// The view is at the flattest tilt or below it.
		EXPECT_GT(PitchOf(game.transform.rotation), 0.0f);
		EXPECT_LT(watch.largestRoll, 1e-3f);
		EXPECT_EQ(watch.LargestJump(), 0.0f);
		EXPECT_TRUE(game.errors.empty());
	}
}

TEST(LookUp, TheViewStraightUpStaysAfterThePauseMenu)
{
	// In the pause menu the game turns its camera to its focus, but its state is the state that the rig wrote. The
	// return keeps the view of the user: straight up, at the same place.
	for (const auto rate : k_Rates)
	{
		SCOPED_TRACE(std::to_string(rate) + " fps");
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		auto harness = Settled(0.0f, deltaTime);
		auto& game = harness.game;
		Watch watch;
		watch.Start(harness);
		RaiseToStraightUp(harness, deltaTime, watch);
		const auto before = game.transform;
		const auto direction = game.direction;
		for (float time = 0.0f; time < 0.5f; time += deltaTime)
		{
			harness.seconds += deltaTime;
			harness.loop.StandBack();
			game.transform.rotation = Multiply(game.transform.rotation, PitchRotation(5.0f));
			game.EndFrame();
		}
		Watch after;
		after.Start(harness);
		RunFor(harness, 1.0f, deltaTime, after);
		EXPECT_LT(Length(game.transform.position - before.position), 1e-3f);
		EXPECT_NEAR(PitchOf(game.transform.rotation), k_StraightUp, 0.05f);
		EXPECT_NEAR(WrapDegrees(game.direction - direction), 0.0f, 1e-3f);
		EXPECT_LT(after.largestHeadingError, 0.05f);
		// A turn after the return goes about a near pivot, not about the far focus of the game.
		game.buttons[2] = true;
		harness.Frame(deltaTime);
		game.turnPerSecond = 90.0f;
		for (float time = 0.0f; time < 0.5f; time += deltaTime)
		{
			game.mouse.x += 300.0f * deltaTime;
			harness.Frame(deltaTime);
			after.Add(harness);
		}
		game.turnPerSecond = 0.0f;
		game.buttons[2] = false;
		RunFor(harness, 0.5f, deltaTime, after);
		const auto row = moa_mod::camera::test::FakeCameraGame::k_StageTable[0];
		const auto limit = 3.0f * row.height / std::sin(row.tilt * k_DegreesToRadians);
		EXPECT_LT(Length(game.transform.position - before.position), 2.0f * limit);
		EXPECT_EQ(after.LargestJump(), 0.0f);
		EXPECT_TRUE(game.errors.empty());
	}
}
