/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

// The camera feel of batch A: the glide after a turn and a pitch, the continuous zoom, the pan speed that follows the
// height, and the edge scroll setting.

#include <cmath>

#include <array>
#include <vector>

#include <gtest/gtest.h>
#include <moa_mod/camera/CameraFeel.h>

#include "CameraLoopHarness.h"

using namespace moa_mod;
using namespace moa_mod::camera;
using moa_mod::camera::test::CameraLoopHarness;
using moa_mod::camera::test::FakeCameraGame;

namespace
{

constexpr std::array k_Rates {30, 60, 144};

const std::vector<StageView> k_Table {{6.0f, 20.0f}, {30.0f, 25.0f}, {90.0f, 45.0f}, {200.0f, 50.0f}, {1200.0f, 50.0f}};

/// The settings of the hand rig with the defaults of batch A.
[[nodiscard]] moa_mod::camera::CameraSettings FeelSettings()
{
	moa_mod::camera::CameraSettings settings;
	settings.enabled = true;
	settings.pitchMode = PitchMode::Hand;
	return settings;
}

[[nodiscard]] CameraLoopHarness Settled(const moa_mod::camera::CameraSettings& settings, float stage, float deltaTime)
{
	CameraLoopHarness harness(settings);
	harness.game.mapMinimum = -1e6f;
	harness.game.mapMaximum = 1e6f;
	harness.game.Start({0.0f, 0.0f, 0.0f}, stage);
	for (int frame = 0; frame < 30; ++frame)
	{
		harness.Frame(deltaTime);
	}
	return harness;
}

void RunFor(CameraLoopHarness& harness, float seconds, float deltaTime)
{
	const auto frames = static_cast<int>(std::lround(seconds / deltaTime));
	for (int frame = 0; frame < frames; ++frame)
	{
		harness.Frame(deltaTime);
	}
}

/// The heading of the camera that the user sees, degrees.
[[nodiscard]] float Heading(const CameraLoopHarness& harness)
{
	const auto forward = Rotate(harness.game.transform.rotation, WorldPoint {0.0f, 0.0f, 1.0f});
	return std::atan2(forward.x, forward.z) / k_DegreesToRadians;
}

/// Turn with the middle button held for one second at 90 degrees for each second, then release the button. Return
/// the turn after the release, degrees.
[[nodiscard]] float TurnAndRelease(CameraLoopHarness& harness, float deltaTime, float afterSeconds)
{
	harness.game.buttons[2] = true;
	harness.game.turnPerSecond = 90.0f;
	RunFor(harness, 1.0f, deltaTime);
	harness.game.buttons[2] = false;
	harness.game.turnPerSecond = 0.0f;
	const auto released = Heading(harness);
	RunFor(harness, afterSeconds, deltaTime);
	return WrapDegrees(Heading(harness) - released);
}

} // namespace

TEST(CameraFeelMaths, StageOfHeightIsTheReverseOfTheHeightOfAStage)
{
	for (float stage = 0.0f; stage <= 4.0f; stage += 0.05f)
	{
		EXPECT_NEAR(StageOfHeight(k_Table, HeightOfStage(k_Table, stage)), stage, 1e-4f) << stage;
	}
	EXPECT_EQ(StageOfHeight(k_Table, 1.0f), 0.0f);
	EXPECT_EQ(StageOfHeight(k_Table, 5000.0f), 4.0f);
}

TEST(CameraFeelMaths, EachNotchChangesTheHeightByTheRatioAtEachHeight)
{
	const StageLimits limits {.minimum = 0.0f, .maximum = 4.0f};
	for (float stage = 0.0f; stage <= 3.5f; stage += 0.1f)
	{
		const auto out = StageAfterNotches(k_Table, stage, -1.0f, 1.25f, limits);
		const auto in = StageAfterNotches(k_Table, stage, 1.0f, 1.25f, limits);
		EXPECT_NEAR(HeightOfStage(k_Table, out) / HeightOfStage(k_Table, stage), 1.25f, 1e-3f) << stage;
		if (stage > 0.3f)
		{
			EXPECT_NEAR(HeightOfStage(k_Table, stage) / HeightOfStage(k_Table, in), 1.25f, 1e-3f) << stage;
		}
	}
	// The limits stop the zoom.
	EXPECT_EQ(StageAfterNotches(k_Table, 0.0f, 1.0f, 1.25f, limits), 0.0f);
	EXPECT_EQ(StageAfterNotches(k_Table, 4.0f, -1.0f, 1.25f, limits), 4.0f);
}

TEST(CameraFeelMaths, TheZoomMovesInTheLogarithmOfTheHeight)
{
	// Half of the time of a step moves half of the ratio of heights, from Production to Map and from Map to Detail.
	for (const auto [from, to] : {std::pair {1.0f, 4.0f}, std::pair {4.0f, 0.0f}})
	{
		for (const auto rate : k_Rates)
		{
			const auto deltaTime = 1.0f / static_cast<float>(rate);
			auto value = from;
			for (int frame = 0; frame < rate; ++frame)
			{
				value = StepStageInHeight(k_Table, value, to, deltaTime, 1.0f);
			}
			// After the smoothing time, 95 percent of the logarithm of the ratio.
			const auto done = std::log(HeightOfStage(k_Table, value) / HeightOfStage(k_Table, from)) /
			                  std::log(HeightOfStage(k_Table, to) / HeightOfStage(k_Table, from));
			EXPECT_NEAR(done, 0.95f, 0.01f) << from << " to " << to << " at " << rate;
		}
	}
}

TEST(CameraFeelMaths, ThePanScaleGivesTheSameViewWidthsAtEachHeight)
{
	for (const auto height : {6.0f, 30.0f, 90.0f, 200.0f, 1200.0f})
	{
		const auto cameraY = 100.0f + height;
		const auto width = ViewWidth(height / std::sin(30.0f * k_DegreesToRadians), 60.0f, 16.0f / 9.0f);
		const auto scale = PanSpeedScale(0.45f, width, cameraY);
		const auto metresPerSecond = k_GameKeyPanSpeed * GamePanHeightFactor(cameraY) * scale;
		EXPECT_NEAR(metresPerSecond / width, 0.45f, 1e-4f) << height;
	}
}

TEST(CameraFeelMaths, TheEdgeScrollDirectionFollowsTheRuleOfTheGame)
{
	const Vector2 screen {.x = 1000.0f, .y = 500.0f};
	auto direction = [&](float x, float y) { return EdgeScrollDirection({x, y, 0.0f}, screen); };
	EXPECT_EQ(direction(500.0f, 250.0f).x, 0.0f);
	EXPECT_EQ(direction(500.0f, 250.0f).y, 0.0f);
	EXPECT_EQ(direction(995.0f, 250.0f).x, 1.0f);
	EXPECT_EQ(direction(995.0f, 250.0f).y, 0.0f);
	EXPECT_EQ(direction(5.0f, 250.0f).x, -1.0f);
	EXPECT_EQ(direction(500.0f, 495.0f).y, 1.0f);
	EXPECT_EQ(direction(500.0f, 5.0f).y, -1.0f);
	// On one side, the other axis follows the place of the cursor along that side.
	EXPECT_NEAR(direction(995.0f, 400.0f).y, 0.75f, 1e-5f);
	EXPECT_NEAR(direction(700.0f, 495.0f).x, 0.5f, 1e-5f);
	// In a corner, both axes scroll.
	EXPECT_EQ(direction(995.0f, 495.0f).x, 1.0f);
	EXPECT_EQ(direction(995.0f, 495.0f).y, 1.0f);
}

TEST(CameraFeelMaths, TheAngleGlideIsTheSameAtEachFrameRate)
{
	for (const auto rate : k_Rates)
	{
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		RateFilter filter;
		for (int frame = 0; frame < rate; ++frame)
		{
			filter.Add(90.0f * deltaTime, deltaTime, 0.1f);
		}
		EXPECT_NEAR(filter.rate, 90.0f, 1e-3f) << rate;
		AngleGlide glide;
		ASSERT_TRUE(glide.Start(filter.rate, 0.01f));
		float total = 0.0f;
		for (int frame = 0; frame < rate * 10 && glide.active; ++frame)
		{
			total += glide.Step(deltaTime, 0.25f, 0.01f);
		}
		EXPECT_NEAR(total, 90.0f * 0.25f, 0.01f) << rate;
		EXPECT_FALSE(glide.active);
	}
}

TEST(CameraFeel, TheTurnGlidesAfterTheReleaseTheSameAtEachFrameRate)
{
	std::vector<float> glides;
	for (const auto rate : k_Rates)
	{
		SCOPED_TRACE(rate);
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		auto harness = Settled(FeelSettings(), 1.0f, deltaTime);
		const auto glide = TurnAndRelease(harness, deltaTime, 3.0f);
		// The game turns by 90 degrees for each second, and the glide continues for the time constant of 0.25 s.
		EXPECT_NEAR(glide, 22.5f, 1.0f);
		glides.push_back(glide);
		// The glide stops.
		const auto stopped = Heading(harness);
		RunFor(harness, 0.5f, deltaTime);
		EXPECT_NEAR(WrapDegrees(Heading(harness) - stopped), 0.0f, 1e-3f);
		EXPECT_TRUE(harness.game.errors.empty());
	}
	EXPECT_NEAR(glides[0], glides[2], 0.02f * glides[2]);
	EXPECT_NEAR(glides[1], glides[2], 0.02f * glides[2]);
}

TEST(CameraFeel, TheTurnGlideKeepsThePivotOnTheScreen)
{
	const auto deltaTime = 1.0f / 60.0f;
	auto harness = Settled(FeelSettings(), 1.0f, deltaTime);
	harness.game.buttons[2] = true;
	harness.game.turnPerSecond = 90.0f;
	RunFor(harness, 1.0f, deltaTime);
	harness.game.buttons[2] = false;
	harness.game.turnPerSecond = 0.0f;
	harness.Frame(deltaTime);
	const auto pivot = harness.loop.Trace().pivot;
	ASSERT_TRUE(pivot.has_value());
	const auto start = ScreenPoint(harness.game.ScreenCameraOf(harness.game.transform), *pivot);
	ASSERT_TRUE(start.has_value());
	float largest = 0.0f;
	int glideFrames = 0;
	for (int frame = 0; frame < 60; ++frame)
	{
		harness.Frame(deltaTime);
		if (harness.loop.Trace().glideTurn == 0.0f)
		{
			continue;
		}
		++glideFrames;
		ASSERT_TRUE(harness.loop.Trace().pivot.has_value());
		EXPECT_NEAR(Length(*harness.loop.Trace().pivot - *pivot), 0.0f, 1e-3f);
		const auto screen = ScreenPoint(harness.game.ScreenCameraOf(harness.game.transform), *pivot);
		ASSERT_TRUE(screen.has_value());
		largest = std::max(largest, std::hypot(screen->x - start->x, screen->y - start->y));
	}
	EXPECT_GT(glideFrames, 10);
	EXPECT_LE(largest, 0.5f);
}

TEST(CameraFeel, ANewPressAKeyPanAWheelNotchAndALandDragStopTheTurnGlide)
{
	const auto deltaTime = 1.0f / 60.0f;
	enum class Stop
	{
		Press,
		KeyPan,
		Wheel,
		LandDrag,
	};
	for (const auto stop : {Stop::Press, Stop::KeyPan, Stop::Wheel, Stop::LandDrag})
	{
		SCOPED_TRACE(static_cast<int>(stop));
		auto harness = Settled(FeelSettings(), 1.0f, deltaTime);
		static_cast<void>(TurnAndRelease(harness, deltaTime, 2.0f * deltaTime));
		ASSERT_NE(harness.loop.Trace().glideTurn, 0.0f);
		switch (stop)
		{
		case Stop::Press:
			harness.game.buttons[2] = true;
			break;
		case Stop::KeyPan:
			harness.game.panKeys = {.x = 1.0f, .y = 0.0f};
			break;
		case Stop::Wheel:
			harness.game.wheel = -0.1f;
			break;
		case Stop::LandDrag:
			harness.game.buttons[1] = true;
			break;
		}
		harness.Frame(deltaTime);
		harness.game.wheel = 0.0f;
		const auto stopped = Heading(harness);
		RunFor(harness, 0.5f, deltaTime);
		EXPECT_EQ(harness.loop.Trace().glideTurn, 0.0f);
		EXPECT_NEAR(WrapDegrees(Heading(harness) - stopped), 0.0f, 1e-3f);
	}
}

TEST(CameraFeel, TheTurnGlideIsOffWithATimeOfZero)
{
	const auto deltaTime = 1.0f / 60.0f;
	auto settings = FeelSettings();
	settings.turnInertia = 0.0;
	// The short smoothing of the mouse gives the last part of the turn after the release; its own test checks that.
	settings.mouseSmoothing = 0.0;
	auto harness = Settled(settings, 1.0f, deltaTime);
	EXPECT_NEAR(TurnAndRelease(harness, deltaTime, 1.0f), 0.0f, 1e-3f);
}

TEST(CameraFeel, ThePitchGlidesAfterTheReleaseTheSameAtEachFrameRate)
{
	std::vector<float> glides;
	for (const auto rate : k_Rates)
	{
		SCOPED_TRACE(rate);
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		auto settings = FeelSettings();
		settings.pitch = true;
		settings.pitchTurnLock = true;
		settings.pitchSmoothing = 0.0;
		auto harness = Settled(settings, 1.0f, deltaTime);
		// A vertical middle drag down at 100 pixels for each second: 10 degrees steeper for each second.
		harness.game.buttons[2] = true;
		harness.Frame(deltaTime);
		const auto frames = static_cast<int>(std::lround(0.5f / deltaTime));
		for (int frame = 0; frame < frames; ++frame)
		{
			harness.game.mouse.y -= 100.0f * deltaTime;
			harness.Frame(deltaTime);
		}
		harness.game.buttons[2] = false;
		const auto released = harness.loop.Trace().raiseTarget;
		RunFor(harness, 3.0f, deltaTime);
		const auto glide = harness.loop.Trace().raiseTarget - released;
		// 10 degrees for each second for 0.25 s: 2.5 degrees steeper.
		EXPECT_NEAR(glide, -2.5f, 0.15f);
		glides.push_back(glide);
		EXPECT_EQ(harness.loop.Trace().glidePitch, 0.0f);
	}
	EXPECT_NEAR(glides[0], glides[2], 0.03f * std::abs(glides[2]));
	EXPECT_NEAR(glides[1], glides[2], 0.03f * std::abs(glides[2]));
}

TEST(CameraFeel, EachWheelNotchChangesTheHeightByTheRatioAtEachStage)
{
	for (const auto rate : k_Rates)
	{
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		for (const auto stage : {0.5f, 1.0f, 1.5f, 2.0f, 2.7f, 3.4f})
		{
			SCOPED_TRACE(testing::Message() << rate << " fps, stage " << stage);
			for (const auto notch : {-0.1f, 0.1f})
			{
				auto harness = Settled(FeelSettings(), stage, deltaTime);
				const auto before = HeightOfStage(k_Table, harness.game.stage);
				harness.game.wheel = notch;
				harness.Frame(deltaTime);
				harness.game.wheel = 0.0f;
				RunFor(harness, 3.0f, deltaTime);
				const auto after = HeightOfStage(k_Table, harness.game.stage);
				EXPECT_NEAR(notch < 0.0f ? after / before : before / after, 1.25f, 0.01f);
				// The game does not move the stage to a whole step.
				EXPECT_GT(std::abs(harness.game.stage - std::round(harness.game.stage)), 1e-3f);
			}
		}
	}
}

TEST(CameraFeel, TheTiltFollowsTheHeightWithNoStepFromDetailToMap)
{
	for (const auto rate : k_Rates)
	{
		SCOPED_TRACE(rate);
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		auto harness = Settled(FeelSettings(), 0.0f, deltaTime);
		float largestTiltError = 0.0f;
		float largestStageStep = 0.0f;
		float lastStage = harness.game.stage;
		for (int notch = 0; notch < 30; ++notch)
		{
			harness.game.wheel = -0.1f;
			harness.Frame(deltaTime);
			harness.game.wheel = 0.0f;
			for (int frame = 0; frame < rate / 8; ++frame)
			{
				harness.Frame(deltaTime);
				// The tilt of the camera is the tilt of the table at the continuous stage, as the game interpolates it.
				const auto tilt = PitchOf(harness.game.transform.rotation);
				largestTiltError =
				    std::max(largestTiltError, std::abs(tilt - InterpolateStage(k_Table, harness.game.stage).tilt));
				largestStageStep = std::max(largestStageStep, std::abs(harness.game.stage - lastStage));
				lastStage = harness.game.stage;
			}
		}
		RunFor(harness, 3.0f, deltaTime);
		EXPECT_NEAR(harness.game.stage, 4.0f, 1e-3f);
		EXPECT_NEAR(PitchOf(harness.game.transform.rotation), 50.0f, 0.05f);
		EXPECT_LT(largestTiltError, 0.05f);
		// The stage moves in small steps that become smaller at a higher frame rate: no jump to a whole stage.
		EXPECT_LT(largestStageStep, 10.0f / static_cast<float>(rate));
		EXPECT_TRUE(harness.game.errors.empty());
	}
}

TEST(CameraFeel, AKeyZoomIsContinuousAndTheGameSettingComesBack)
{
	const auto deltaTime = 1.0f / 60.0f;
	auto harness = Settled(FeelSettings(), 1.0f, deltaTime);
	harness.game.zoomKeys = 1.0f;
	float largestStep = 0.0f;
	auto last = harness.game.stage;
	for (int frame = 0; frame < 30; ++frame)
	{
		harness.Frame(deltaTime);
		largestStep = std::max(largestStep, std::abs(harness.game.stage - last));
		last = harness.game.stage;
		EXPECT_FALSE(harness.game.dollySmooth);
	}
	harness.game.zoomKeys = 0.0f;
	RunFor(harness, 2.0f, deltaTime);
	EXPECT_GT(harness.game.stage, 1.2f);
	EXPECT_LT(largestStep, 0.1f);

	// With the continuous zoom off, the keys zoom by whole stages, as in the game.
	auto settings = FeelSettings();
	settings.continuousZoom = false;
	auto old = Settled(settings, 1.0f, deltaTime);
	old.game.zoomKeys = 1.0f;
	old.Frame(deltaTime);
	old.game.zoomKeys = 0.0f;
	RunFor(old, 3.0f, deltaTime);
	EXPECT_NEAR(old.game.stage, 2.0f, 1e-3f);
}

TEST(CameraFeel, TheKeyPanMovesTheSamePartOfTheViewAtDetailAndAtMap)
{
	for (const auto rate : k_Rates)
	{
		std::vector<float> speeds;
		for (const auto stage : {0.0f, 1.0f, 4.0f})
		{
			SCOPED_TRACE(testing::Message() << rate << " fps, stage " << stage);
			const auto deltaTime = 1.0f / static_cast<float>(rate);
			// The test measures the speed of the pan far from the edge of the map, with no soft edge.
			auto settings = FeelSettings();
			settings.edgeMargin = 0.0;
			auto harness = Settled(settings, stage, deltaTime);
			harness.game.panKeys = {.x = 1.0f, .y = 0.0f};
			RunFor(harness, 3.0f, deltaTime);
			const auto start = harness.game.transform.position;
			RunFor(harness, 1.0f, deltaTime);
			const auto move = harness.game.transform.position - start;
			const auto distance = Length(harness.game.transform.position - harness.game.focus);
			const auto width =
			    ViewWidth(distance, FakeCameraGame::k_FieldOfView, harness.game.screenWidth / harness.game.screenHeight);
			speeds.push_back(std::hypot(move.x, move.z) / width);
			// The move speed of the game comes back after each frame.
			EXPECT_EQ(harness.game.moveSpeed, 1.0f);
			EXPECT_TRUE(harness.game.errors.empty());
		}
		for (const auto speed : speeds)
		{
			EXPECT_NEAR(speed, 0.45f, 0.01f) << rate;
		}
	}
}

TEST(CameraFeel, WithNoPanSpeedTheKeyPanIsTheGameSpeed)
{
	const auto deltaTime = 1.0f / 60.0f;
	auto settings = FeelSettings();
	settings.panSpeed = 0.0;
	auto harness = Settled(settings, 0.0f, deltaTime);
	harness.game.panKeys = {.x = 1.0f, .y = 0.0f};
	RunFor(harness, 3.0f, deltaTime);
	const auto start = harness.game.transform.position;
	RunFor(harness, 1.0f, deltaTime);
	const auto move = harness.game.transform.position - start;
	EXPECT_NEAR(std::hypot(move.x, move.z), k_GameKeyPanSpeed * GamePanHeightFactor(start.y), 1.0f);
}

TEST(CameraFeel, TheEdgeScrollSettingRemovesTheEdgeScroll)
{
	for (const auto edgeScroll : {true, false})
	{
		for (const auto rate : k_Rates)
		{
			SCOPED_TRACE(testing::Message() << rate << " fps, edge scroll " << edgeScroll);
			const auto deltaTime = 1.0f / static_cast<float>(rate);
			auto settings = FeelSettings();
			settings.edgeScroll = edgeScroll;
			auto harness = Settled(settings, 1.0f, deltaTime);
			harness.game.edgeScrollActive = true;
			harness.game.mouse = {harness.game.screenWidth - 5.0f, harness.game.screenHeight * 0.5f, 0.0f};
			const auto start = harness.game.transform.position;
			RunFor(harness, 1.0f, deltaTime);
			const auto move = Length(harness.game.transform.position - start);
			if (edgeScroll)
			{
				EXPECT_GT(move, 10.0f);
			}
			else
			{
				EXPECT_LT(move, 0.01f);
				EXPECT_TRUE(harness.loop.Trace().edgeScrollRemoved.has_value());
			}
			EXPECT_TRUE(harness.game.errors.empty());
		}
	}
}
