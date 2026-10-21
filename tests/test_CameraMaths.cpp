/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <cmath>

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <moa_mod/camera/CameraDrag.h>
#include <moa_mod/camera/CameraInertia.h>
#include <moa_mod/camera/CameraPitch.h>
#include <moa_mod/camera/CameraPivot.h>

using namespace moa_mod;
using namespace moa_mod::camera;

namespace
{

void ExpectNear(WorldPoint actual, WorldPoint expected, float tolerance = 1e-4f)
{
	EXPECT_NEAR(actual.x, expected.x, tolerance);
	EXPECT_NEAR(actual.y, expected.y, tolerance);
	EXPECT_NEAR(actual.z, expected.z, tolerance);
}

/// The pose that the game makes for a focus and a heading: the camera is behind and above the focus, turned by the
/// heading about y, and looks at the focus.
[[nodiscard]] CameraPose GamePose(WorldPoint focus, float direction)
{
	const auto position = focus + Rotate(YawRotation(direction), WorldPoint {0.0f, 30.0f, -40.0f});
	return {.focus = focus,
	        .position = position,
	        .rotation = LookRotation(focus - position, {0.0f, 1.0f, 0.0f}),
	        .direction = direction};
}

} // namespace

TEST(CameraPivot, TurnsTheCameraLikeTheGameMakesANewHeading)
{
	const WorldPoint focus {100.0f, 5.0f, 50.0f};
	const auto turned = TurnCamera(GamePose(focus, 20.0f), focus, 35.0f);
	const auto expected = GamePose(focus, 55.0f);
	ExpectNear(turned.position, expected.position);
	ExpectNear(Rotate(turned.rotation, {0.0f, 0.0f, 1.0f}), Rotate(expected.rotation, {0.0f, 0.0f, 1.0f}));
	EXPECT_FLOAT_EQ(turned.direction, 55.0f);
}

TEST(CameraPivot, KeepsTheHandWhereItWasOnTheScreen)
{
	// The game turned the camera about the focus from 10 to 40 degrees. The mod moves the turn to the hand.
	const WorldPoint focus {100.0f, 0.0f, 50.0f};
	const WorldPoint hand {120.0f, 3.0f, 70.0f};
	const auto before = GamePose(focus, 10.0f);
	const auto game = GamePose(focus, 40.0f);
	const auto pose = ChangeTurn(game, {.startDirection = 10.0f, .direction = 40.0f, .pivot = hand});
	// In the frame of the camera, the hand is where it was before the turn.
	ExpectNear(Rotate(Inverse(pose.rotation), hand - pose.position), Rotate(Inverse(before.rotation), hand - before.position),
	           1e-3f);
	// The rotation is that of the game, and the focus moved with the camera.
	ExpectNear(Rotate(pose.rotation, {0.0f, 0.0f, 1.0f}), Rotate(game.rotation, {0.0f, 0.0f, 1.0f}));
	ExpectNear(pose.position - pose.focus, game.position - game.focus);
	ExpectNear(pose.focus, FocusForHandPivot(focus, hand, 30.0f));
}

TEST(CameraPivot, KeepsTheHandWithASmoothedTurn)
{
	// The game turned 30 degrees, but the smoothed heading is only 12 degrees from the start.
	const WorldPoint focus {0.0f, 0.0f, 0.0f};
	const WorldPoint hand {-15.0f, 0.0f, 25.0f};
	const auto before = GamePose(focus, 0.0f);
	const auto pose = ChangeTurn(GamePose(focus, 30.0f), {.startDirection = 0.0f, .direction = 12.0f, .pivot = hand});
	EXPECT_FLOAT_EQ(pose.direction, 12.0f);
	ExpectNear(Rotate(Inverse(pose.rotation), hand - pose.position), Rotate(Inverse(before.rotation), hand - before.position),
	           1e-3f);
}

TEST(CameraPivot, InvertsTheTurnAboutTheHandWithTheSetting)
{
	const WorldPoint focus {0.0f, 0.0f, 0.0f};
	const WorldPoint hand {10.0f, 0.0f, 0.0f};
	const auto game = GamePose(focus, 30.0f);
	const auto normal = ChangeTurn(game, {.startDirection = 0.0f, .direction = 30.0f, .pivot = hand});
	const auto inverted = ChangeTurn(game, {.startDirection = 0.0f, .direction = 30.0f, .pivot = hand, .invertPivot = true});
	ExpectNear(normal.focus, FocusForHandPivot(focus, hand, 30.0f));
	ExpectNear(inverted.focus, FocusForHandPivot(focus, hand, -30.0f));
	EXPECT_FLOAT_EQ(inverted.direction, 30.0f);
}

TEST(CameraPivot, ChangesNothingWithNoTurnAndNoPivot)
{
	const auto game = GamePose({5.0f, 1.0f, 5.0f}, -170.0f);
	const auto pose = ChangeTurn(game, {.startDirection = -170.0f, .direction = -170.0f});
	ExpectNear(pose.position, game.position);
	ExpectNear(pose.focus, game.focus);
}

TEST(CameraPivot, SmoothsTheHeadingAcrossTheWrap)
{
	HeadingSmoother heading;
	heading.Reset(170.0f);
	// The game turned 20 degrees in one frame, across 180 degrees.
	auto value = heading.Step(170.0f, -170.0f, 0.016f, 0.3f);
	EXPECT_GT(WrapDegrees(value - 170.0f), 0.0f);
	EXPECT_LT(WrapDegrees(value - 170.0f), 20.0f);
	for (int frame = 0; frame < 200; ++frame)
	{
		value = heading.Step(value, value, 0.016f, 0.3f);
	}
	EXPECT_NEAR(WrapDegrees(value + 170.0f), 0.0f, 1e-3f);
}

TEST(CameraPivot, SmoothsTheHeadingTheSameAtEachFrameRate)
{
	for (const auto rate : {30, 144})
	{
		HeadingSmoother heading;
		heading.Reset(0.0f);
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		auto value = heading.Step(0.0f, 90.0f, deltaTime, 0.5f);
		for (int frame = 1; frame < rate / 2; ++frame)
		{
			value = heading.Step(value, value, deltaTime, 0.5f);
		}
		// After the smoothing time, 5 percent of the turn waits.
		EXPECT_NEAR(value, 90.0f * 0.95f, 0.2f);
	}
}

TEST(CameraPivot, KeepsTheZoomTargetInTheLimitsOfTheGame)
{
	const auto limits = StageLimitsFor(5, 0.0f, -1.0f);
	EXPECT_FLOAT_EQ(limits.minimum, 0.0f);
	EXPECT_FLOAT_EQ(limits.maximum, 4.0f);
	const auto clamped = StageLimitsFor(5, 0.5f, 2.0f);
	EXPECT_FLOAT_EQ(clamped.minimum, 0.5f);
	EXPECT_FLOAT_EQ(clamped.maximum, 2.0f);
	EXPECT_FLOAT_EQ(StageLimitsFor(5, 3.0f, 2.0f).minimum, 2.0f);

	ZoomSmoother zoom;
	zoom.Reset(1.0f);
	zoom.AddToTarget(10.0f, limits);
	EXPECT_FLOAT_EQ(zoom.target, 4.0f);
	zoom.AddToTarget(-10.0f, limits);
	EXPECT_FLOAT_EQ(zoom.target, 0.0f);
	// A smaller limit pulls the target and the value into the new range.
	zoom.Reset(4.0f);
	EXPECT_FLOAT_EQ(zoom.Step(0.016f, 0.3f, clamped), 2.0f);
}

TEST(CameraPivot, ZoomsInWhenTheWheelTurnsAway)
{
	EXPECT_FLOAT_EQ(WheelStageChange(0.1f, 0.5f), -0.5f);
	EXPECT_FLOAT_EQ(WheelStageChange(-0.2f, 0.5f), 1.0f);
	EXPECT_FLOAT_EQ(WheelStageChange(0.0f, 0.5f), 0.0f);
}

TEST(CameraPivot, SmoothsTheZoomTheSameAtEachFrameRate)
{
	const StageLimits limits {.minimum = 0.0f, .maximum = 4.0f};
	for (const auto rate : {30, 144})
	{
		ZoomSmoother zoom;
		zoom.Reset(1.0f);
		zoom.AddToTarget(2.0f, limits);
		float value = 1.0f;
		for (int frame = 0; frame < rate / 2; ++frame)
		{
			value = zoom.Step(1.0f / static_cast<float>(rate), 0.5f, limits);
		}
		EXPECT_NEAR(value, 3.0f - 2.0f * 0.05f, 2e-3f);
	}
}

TEST(CameraPivot, TurnsLikeAUnityRotationAboutY)
{
	// Unity turns the x axis to -z for +90 degrees about y.
	const auto turned = TurnAboutPivot({1.0f, 5.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 90.0f);
	EXPECT_NEAR(turned.x, 0.0f, 1e-5f);
	EXPECT_FLOAT_EQ(turned.y, 5.0f);
	EXPECT_NEAR(turned.z, -1.0f, 1e-5f);
}

TEST(CameraPivot, KeepsTheHandFixedAndTheDistanceToTheFocus)
{
	const WorldPoint focus {10.0f, 2.0f, 20.0f};
	const WorldPoint hand {14.0f, 7.0f, 17.0f};
	const auto moved = FocusForHandPivot(focus, hand, 37.0f);
	const auto before = std::hypot(focus.x - hand.x, focus.z - hand.z);
	const auto after = std::hypot(moved.x - hand.x, moved.z - hand.z);
	EXPECT_NEAR(before, after, 1e-4f);
	EXPECT_FLOAT_EQ(moved.y, focus.y);
	const auto same = FocusForHandPivot(focus, focus, 37.0f);
	EXPECT_NEAR(same.x, focus.x, 1e-5f);
	EXPECT_NEAR(same.z, focus.z, 1e-5f);
	const auto back = FocusForHandPivot(moved, hand, -37.0f);
	EXPECT_NEAR(back.x, focus.x, 1e-4f);
	EXPECT_NEAR(back.z, focus.z, 1e-4f);
}

TEST(CameraPivot, SmoothsTheSameAtEachFrameRate)
{
	EXPECT_FLOAT_EQ(SmoothingStep(0.016f, 0.0f), 1.0f);
	// After the smoothing time, 5 percent of the distance stays, at 30 and at 144 frames each second.
	for (const auto rate : {30, 144})
	{
		float remaining = 1.0f;
		for (int frame = 0; frame < rate / 2; ++frame)
		{
			remaining *= 1.0f - SmoothingStep(1.0f / static_cast<float>(rate), 0.5f);
		}
		EXPECT_NEAR(remaining, 0.05f, 1e-3f);
	}
}

TEST(LookUp, PermitsTheFullLookUpOnlyNearTheGround)
{
	const LookUpLimits limits {.maxAboveHorizon = 15.0f, .fullStage = 0.5f, .zeroStage = 1.5f};
	EXPECT_FLOAT_EQ(LookUpFade(0.0f, limits), 1.0f);
	EXPECT_FLOAT_EQ(LookUpFade(0.5f, limits), 1.0f);
	EXPECT_FLOAT_EQ(LookUpFade(1.0f, limits), 0.5f);
	EXPECT_FLOAT_EQ(LookUpFade(1.5f, limits), 0.0f);
	EXPECT_FLOAT_EQ(LookUpFade(4.0f, limits), 0.0f);
	// With no pitch, the look up starts at the tilt of the game. At the closest zoom the game looks down by 20
	// degrees; the view centre can go 15 degrees above the horizon.
	const PitchLimits lookUpOnly {.pitch = false, .lookUp = true, .lookUpLimits = limits};
	const auto closest = PitchRangeFor(0.0f, 20.0f, lookUpOnly);
	EXPECT_FLOAT_EQ(closest.steepest, 0.0f);
	EXPECT_FLOAT_EQ(closest.flattest, 0.0f);
	EXPECT_FLOAT_EQ(closest.highest, 35.0f);
	EXPECT_FLOAT_EQ(PitchRangeFor(3.0f, 50.0f, lookUpOnly).highest, 0.0f);
	// The fade decreases at each larger stage.
	float previous = PitchRangeFor(0.5f, 20.0f, lookUpOnly).highest;
	for (float stage = 0.6f; stage <= 1.5f; stage += 0.1f)
	{
		const auto value = PitchRangeFor(stage, 20.0f, lookUpOnly).highest;
		EXPECT_LE(value, previous);
		previous = value;
	}
}

TEST(LookUp, KeepsTheInputInTheLimitAndEasesBack)
{
	const PitchRange range {.steepest = 0.0f, .flattest = 0.0f, .highest = 35.0f};
	// The input cannot go above the limit or below zero.
	EXPECT_FLOAT_EQ(StepPitchTarget(30.0f, 10.0f, range, 0.016f, 0.4f), 35.0f);
	EXPECT_FLOAT_EQ(StepPitchTarget(5.0f, -10.0f, range, 0.016f, 0.4f), 0.0f);
	EXPECT_FLOAT_EQ(StepPitchTarget(10.0f, 5.0f, range, 0.016f, 0.4f), 15.0f);
	// After a zoom out the limit is zero: the look up eases back, the same at each frame rate.
	for (const auto rate : {30, 144})
	{
		float lookUp = 30.0f;
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		for (int frame = 0; frame < static_cast<int>(0.4f * static_cast<float>(rate) + 0.5f); ++frame)
		{
			lookUp = StepPitchTarget(lookUp, 1.0f, PitchRange {}, deltaTime, 0.4f);
		}
		EXPECT_NEAR(lookUp, 30.0f * 0.05f, 0.1f);
	}
}

TEST(LookUp, TurnsTheViewUpAboutTheCamera)
{
	// The game looks down by 20 degrees. A look up of 35 degrees puts the view centre 15 degrees above the horizon.
	const auto down = LookRotation(Rotate(YawRotation(70.0f), WorldPoint {0.0f, -std::sin(20.0f * k_DegreesToRadians),
	                                                                      std::cos(20.0f * k_DegreesToRadians)}),
	                               {0.0f, 1.0f, 0.0f});
	const auto forward = Rotate(Multiply(down, LookUpRotation(35.0f)), WorldPoint {0.0f, 0.0f, 1.0f});
	EXPECT_NEAR(std::asin(forward.y) / k_DegreesToRadians, 15.0f, 1e-3f);
	// The heading does not change.
	EXPECT_NEAR(std::atan2(forward.x, forward.z) / k_DegreesToRadians, 70.0f, 1e-3f);
}

namespace
{

/// The rig of the Production stage of the game: 30 m high, 25 degrees down.
[[nodiscard]] CameraRig ProductionRig(WorldPoint focus, float direction)
{
	return {.focus = focus, .direction = direction, .tilt = 25.0f, .distance = StageDistance(30.0f, 25.0f)};
}

/// Return where a point is in the frame of the camera of a rig. The same result before and after a change means
/// that the point stays at the same place on the screen.
[[nodiscard]] WorldPoint InCameraFrame(const CameraRig& rig, WorldPoint point)
{
	return Rotate(Inverse(RigRotation(rig)), point - RigPosition(rig));
}

const PitchLimits k_Limits {.pitch = true,
                            .minimumTilt = 5.0f,
                            .maximumTilt = 85.0f,
                            .lookUp = true,
                            .lookUpLimits = {.maxAboveHorizon = 15.0f, .fullStage = 0.5f, .zeroStage = 1.5f}};

} // namespace

TEST(CameraPitch, PlacesTheCameraLikeTheGame)
{
	// The game puts the camera at the focus plus Euler(0, direction, 0) * (0, h, -h / tan(tilt)) and looks at the
	// focus.
	const WorldPoint focus {100.0f, 92.0f, 50.0f};
	const auto rig = ProductionRig(focus, 40.0f);
	const auto expected =
	    focus + Rotate(YawRotation(40.0f), WorldPoint {0.0f, 30.0f, -30.0f / std::tan(25.0f * k_DegreesToRadians)});
	ExpectNear(RigPosition(rig), expected, 1e-3f);
	ExpectNear(Rotate(RigRotation(rig), {0.0f, 0.0f, 1.0f}), Normalize(focus - expected));
	EXPECT_FLOAT_EQ(StageDistance(30.0f, 0.0f), 0.0f);
}

TEST(CameraPitch, KeepsThePivotAtItsPlaceOnTheScreen)
{
	const WorldPoint focus {100.0f, 92.0f, 50.0f};
	const auto rig = ProductionRig(focus, 30.0f);
	// Hand points in front of the focus, behind it, to the side, and on a hill.
	for (const auto pivot : {focus + Rotate(YawRotation(30.0f), WorldPoint {0.0f, 0.0f, 15.0f}),
	                         focus + Rotate(YawRotation(30.0f), WorldPoint {-8.0f, 0.0f, -12.0f}),
	                         focus + Rotate(YawRotation(30.0f), WorldPoint {20.0f, 4.0f, 3.0f})})
	{
		for (const auto tilt : {15.0f, 40.0f, 70.0f})
		{
			const auto orbit = OrbitRig(rig, pivot, tilt);
			ASSERT_TRUE(orbit);
			ExpectNear(InCameraFrame(*orbit, pivot), InCameraFrame(rig, pivot), 2e-3f);
			EXPECT_FLOAT_EQ(orbit->tilt, tilt);
			// The camera keeps its distance to the pivot. The focus stays at its height and moves only along the
			// heading.
			EXPECT_NEAR(Length(RigPosition(*orbit) - pivot), Length(RigPosition(rig) - pivot), 2e-3f);
			EXPECT_FLOAT_EQ(orbit->focus.y, focus.y);
			const auto shift = Rotate(YawRotation(-30.0f), orbit->focus - focus);
			EXPECT_NEAR(shift.x, 0.0f, 1e-3f);
		}
	}
}

TEST(CameraPitch, KeepsTheDistanceForATurnAboutTheFocus)
{
	const auto rig = ProductionRig({10.0f, 95.0f, -20.0f}, -120.0f);
	const auto orbit = OrbitRig(rig, rig.focus, 60.0f);
	ASSERT_TRUE(orbit);
	ExpectNear(orbit->focus, rig.focus, 1e-3f);
	EXPECT_NEAR(orbit->distance, rig.distance, 1e-3f);
	// With no pivot, the same.
	const auto pitched = PitchRig(rig, std::nullopt, 60.0f, 0.0f, 1000.0f);
	ExpectNear(pitched.focus, rig.focus);
	EXPECT_FLOAT_EQ(pitched.distance, rig.distance);
	EXPECT_FLOAT_EQ(pitched.tilt, 60.0f);
}

TEST(CameraPitch, TurnsAboutTheFocusWhenThePivotCannotHoldTheView)
{
	// A hand point far in front of the focus: a flat tilt about it puts the camera below the focus.
	const auto rig = ProductionRig({0.0f, 90.0f, 0.0f}, 0.0f);
	const WorldPoint far {0.0f, 90.0f, 60.0f};
	EXPECT_FALSE(OrbitRig(rig, far, 5.0f));
	const auto pitched = PitchRig(rig, far, 5.0f, 0.5f * rig.distance, 2.0f * rig.distance);
	EXPECT_FLOAT_EQ(pitched.tilt, 5.0f);
	EXPECT_GE(pitched.distance, 0.5f * rig.distance - 1e-3f);
	EXPECT_LE(pitched.distance, 2.0f * rig.distance + 1e-3f);
	EXPECT_GT(RigPosition(pitched).y, rig.focus.y);
}

TEST(CameraPitch, HoldsThePivotOverManySmoothedFrames)
{
	// A drag of 12 degrees toward the horizon, smoothed over many frames, about one hand point.
	const auto start = ProductionRig({0.0f, 90.0f, 0.0f}, 75.0f);
	const auto pivot = start.focus + Rotate(YawRotation(75.0f), WorldPoint {6.0f, 0.0f, 5.0f});
	const auto range = PitchRangeFor(1.0f, 25.0f, k_Limits);
	auto rig = start;
	const auto target = StepPitchTarget(0.0f, 12.0f, range, 1.0f / 60.0f, 0.4f);
	float value = 0.0f;
	for (int frame = 0; frame < 120; ++frame)
	{
		const auto previous = SplitPitch(value, 25.0f, range);
		value = FollowValue(value, target, 1.0f / 60.0f, 0.1f);
		rig.tilt = previous.tilt;
		rig = PitchRig(rig, pivot, SplitPitch(value, 25.0f, range).tilt, 0.5f * start.distance, 2.0f * start.distance);
	}
	EXPECT_NEAR(rig.tilt, 13.0f, 1e-3f);
	ExpectNear(InCameraFrame(rig, pivot), InCameraFrame(start, pivot), 5e-3f);
}

TEST(CameraPitch, JoinsThePitchAndTheLookUpWithNoStep)
{
	// At the closest zoom the game looks down by 20 degrees. A raise of 15 degrees gives the flattest tilt (5).
	const auto range = PitchRangeFor(0.0f, 20.0f, k_Limits);
	EXPECT_FLOAT_EQ(range.steepest, -65.0f);
	EXPECT_FLOAT_EQ(range.flattest, 15.0f);
	EXPECT_FLOAT_EQ(range.highest, 15.0f + 20.0f);
	const auto atJoin = SplitPitch(15.0f, 20.0f, range);
	EXPECT_FLOAT_EQ(atJoin.tilt, 5.0f);
	EXPECT_FLOAT_EQ(atJoin.lookUp, 0.0f);
	// The view centre moves the same for the same raise on the two sides of the join.
	auto elevation = [](PitchView view) { return view.lookUp - view.tilt; };
	for (float raise = 10.0f; raise <= 20.0f; raise += 0.5f)
	{
		EXPECT_NEAR(elevation(SplitPitch(raise + 0.5f, 20.0f, range)) - elevation(SplitPitch(raise, 20.0f, range)), 0.5f,
		            1e-4f);
	}
	// At the top, the view centre is 15 degrees above the horizon.
	EXPECT_FLOAT_EQ(elevation(SplitPitch(100.0f, 20.0f, range)), 15.0f);
	// Far from the ground there is no look up: the raise stops at the flattest tilt.
	const auto combat = PitchRangeFor(2.0f, 45.0f, k_Limits);
	EXPECT_FLOAT_EQ(combat.highest, combat.flattest);
}

TEST(CameraPitch, KeepsTheOffsetInTheLimitsAtEachStage)
{
	// A raise is an offset from the tilt of each stage, in the limits of the settings.
	const auto detail = PitchRangeFor(0.0f, 20.0f, k_Limits);
	const auto map = PitchRangeFor(4.0f, 50.0f, k_Limits);
	EXPECT_FLOAT_EQ(SplitPitch(-40.0f, 20.0f, detail).tilt, 60.0f);
	EXPECT_FLOAT_EQ(SplitPitch(-40.0f, 50.0f, map).tilt, 85.0f);
	EXPECT_FLOAT_EQ(SplitPitch(10.0f, 20.0f, detail).tilt, 10.0f);
	EXPECT_FLOAT_EQ(SplitPitch(10.0f, 50.0f, map).tilt, 40.0f);
	// A raise of zero is the tilt of the game, also outside the limits of the settings.
	const PitchLimits narrow {.pitch = true, .minimumTilt = 30.0f, .maximumTilt = 40.0f};
	EXPECT_FLOAT_EQ(SplitPitch(0.0f, 20.0f, PitchRangeFor(0.0f, 20.0f, narrow)).tilt, 20.0f);
	EXPECT_FLOAT_EQ(SplitPitch(0.0f, 50.0f, PitchRangeFor(4.0f, 50.0f, narrow)).tilt, 50.0f);
	// The input cannot push the target past the steepest tilt.
	EXPECT_FLOAT_EQ(StepPitchTarget(-60.0f, -10.0f, detail, 0.016f, 0.4f), -65.0f);
}

TEST(CameraPitch, SmoothsThePitchTheSameAtEachFrameRate)
{
	std::vector<float> results;
	for (const auto rate : {30, 144})
	{
		float value = 0.0f;
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		// One sixth of a second: 5 frames at 30 per second and 24 frames at 144 per second.
		for (int frame = 0; frame < rate / 6; ++frame)
		{
			value = FollowValue(value, 20.0f, deltaTime, 0.25f);
		}
		results.push_back(value);
	}
	// 1 - 0.05 ^ (2 / 3) of the way.
	EXPECT_NEAR(results[0], 20.0f * (1.0f - std::pow(0.05f, 2.0f / 3.0f)), 1e-3f);
	EXPECT_NEAR(results[0], results[1], 1e-3f);
}

TEST(CameraPitch, KeepsTheCameraAboveTheLand)
{
	// The camera must go up 3 m. The raise follows smoothly, but never lets the camera go into the land.
	const float clearance = 1.5f;
	const auto first = StepClearance(0.0f, 3.0f, clearance, 1.0f / 144.0f);
	EXPECT_GE(first, 3.0f - clearance);
	EXPECT_LT(first, 3.0f);
	for (const auto rate : {30, 144})
	{
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		float value = 0.0f;
		for (int frame = 0; frame < rate / 2; ++frame)
		{
			value = StepClearance(value, 3.0f, clearance, deltaTime);
		}
		EXPECT_NEAR(value, 3.0f, 1e-3f);
		// The land goes away: the raise goes down to zero smoothly.
		const auto next = StepClearance(value, -2.0f, clearance, deltaTime);
		EXPECT_LT(next, value);
		EXPECT_GT(next, 2.0f);
		for (int frame = 0; frame < rate * 2; ++frame)
		{
			value = StepClearance(value, -2.0f, clearance, deltaTime);
		}
		EXPECT_NEAR(value, 0.0f, 1e-2f);
	}
}

TEST(CameraPitch, ResetsToTheViewOfTheGame)
{
	// A reset sets the target to zero. The raise eases back, and the rig is again the rig of the game.
	const auto range = PitchRangeFor(1.0f, 25.0f, k_Limits);
	const auto game = ProductionRig({0.0f, 90.0f, 0.0f}, 10.0f);
	float value = 18.0f;
	auto rig = game;
	rig.tilt = SplitPitch(value, 25.0f, range).tilt;
	float distanceScale = 1.3f;
	for (int frame = 0; frame < 60; ++frame)
	{
		const auto previous = SplitPitch(value, 25.0f, range);
		value = FollowValue(value, StepPitchTarget(0.0f, 0.0f, range, 1.0f / 60.0f, 0.4f), 1.0f / 60.0f, 0.1f);
		distanceScale = FollowValue(distanceScale, 1.0f, 1.0f / 60.0f, 0.1f);
		rig.tilt = previous.tilt;
		rig = PitchRig(rig, std::nullopt, SplitPitch(value, 25.0f, range).tilt, 0.0f, 1000.0f);
		rig.distance = game.distance * distanceScale;
	}
	EXPECT_NEAR(value, 0.0f, 1e-3f);
	ExpectNear(RigPosition(rig), RigPosition(game), 1e-2f);
}

namespace
{

/// A zoom stage of the game: the height of the camera above the focus and the tilt.
struct Stage
{
	const char* name;
	float height;
	float tilt;
};

constexpr std::array<Stage, 3> k_Stages {{{"Detail", 6.0f, 20.0f}, {"Production", 30.0f, 25.0f}, {"Combat", 90.0f, 45.0f}}};

/// The level of the land in the drag tests.
constexpr float k_LandLevel = 92.0f;

/// A land with hills, to change the clearance of the camera as it moves.
[[nodiscard]] float HillHeight(float x, float z)
{
	return k_LandLevel + 3.0f * std::sin(x * 0.21f) * std::cos(z * 0.17f);
}

/// The camera that the user sees: the rig of the stage at another tilt with the same distance, raised above the
/// hills, and turned up by the look up.
struct ShownCamera
{
	Stage stage;
	float tilt = 0.0f;
	float lookUp = 0.0f;
	bool clearance = false;

	[[nodiscard]] ScreenCamera At(WorldPoint focus) const
	{
		const CameraRig rig {
		    .focus = focus, .direction = 30.0f, .tilt = tilt, .distance = StageDistance(stage.height, stage.tilt)};
		auto position = RigPosition(rig);
		if (clearance)
		{
			// The rule of the game: the camera stays 2 m plus 1.5 m above the land.
			position.y = std::max(position.y, HillHeight(position.x, position.z) + 3.5f);
		}
		return {.position = position, .rotation = Multiply(RigRotation(rig), LookUpRotation(lookUp))};
	}
};

/// The land point under a cursor, as the game finds it on a level land.
[[nodiscard]] WorldPoint LandUnder(const ScreenCamera& camera, Vector2 cursor)
{
	return PointOnLevel(ScreenRay(camera, cursor), k_LandLevel);
}

/// The land drag of the game with no limits: the ray meets the level of the grab point.
[[nodiscard]] WorldPoint GameHoldShift(const Ray& ray, WorldPoint grab)
{
	const auto distance = (grab.y - ray.origin.y) / ray.direction.y;
	const auto point = ray.origin + ray.direction * distance;
	return {grab.x - point.x, 0.0f, grab.z - point.z};
}

[[nodiscard]] float ScreenError(const ScreenCamera& camera, WorldPoint grab, Vector2 cursor)
{
	const auto point = ScreenPoint(camera, grab);
	if (!point)
	{
		return 1e9f;
	}
	return std::hypot(point->x - cursor.x, point->y - cursor.y);
}

[[nodiscard]] std::vector<ShownCamera> ShownCameras()
{
	std::vector<ShownCamera> cameras;
	for (const auto& stage : k_Stages)
	{
		for (const auto tilt : {stage.tilt, 5.0f, 85.0f})
		{
			cameras.push_back({.stage = stage, .tilt = tilt, .lookUp = 0.0f, .clearance = true});
		}
	}
	cameras.push_back({.stage = k_Stages[0], .tilt = 5.0f, .lookUp = 15.0f, .clearance = true});
	return cameras;
}

} // namespace

TEST(CameraDrag, FindsTheSamePointOnTheScreenAsTheRay)
{
	const ScreenCamera camera {.position = {10.0f, 120.0f, -30.0f},
	                           .rotation = RigRotation({.focus = {}, .direction = -40.0f, .tilt = 35.0f, .distance = 1.0f})};
	for (const Vector2 cursor : {Vector2 {960.0f, 540.0f}, Vector2 {100.0f, 900.0f}, Vector2 {1800.0f, 50.0f}})
	{
		const auto ray = ScreenRay(camera, cursor);
		const auto point = ScreenPoint(camera, ray.origin + ray.direction * 75.0f);
		ASSERT_TRUE(point);
		EXPECT_NEAR(point->x, cursor.x, 1e-2f);
		EXPECT_NEAR(point->y, cursor.y, 1e-2f);
	}
	// The same screen point for the camera at another position and rotation.
	const ScreenCamera other {.position = {-5.0f, 95.0f, 12.0f},
	                          .rotation = Multiply(YawRotation(70.0f), PitchRotation(10.0f))};
	const auto moved =
	    MoveRay(ScreenRay(camera, {300.0f, 200.0f}), camera.position, camera.rotation, other.position, other.rotation);
	const auto expected = ScreenRay(other, {300.0f, 200.0f});
	ExpectNear(moved.origin, expected.origin);
	ExpectNear(moved.direction, expected.direction);
}

TEST(CameraDrag, MovesNothingForAPressWithNoMotion)
{
	// A right click with no motion: the grab point is the land under the cursor, and the hold must not move the
	// camera, at each stage, tilt and look up.
	const WorldPoint focus {40.0f, k_LandLevel, 25.0f};
	int checks = 0;
	for (const auto& shown : ShownCameras())
	{
		const auto camera = shown.At(focus);
		for (const Vector2 cursor : {Vector2 {960.0f, 300.0f}, Vector2 {200.0f, 100.0f}, Vector2 {1700.0f, 250.0f}})
		{
			const auto ray = ScreenRay(camera, cursor);
			if (ray.direction.y > -0.05f)
			{
				continue; // The cursor is not on the land.
			}
			const auto grab = LandUnder(camera, cursor);
			ExpectNear(HoldShift(ray, grab), {}, 2e-3f);
			++checks;
		}
	}
	EXPECT_GT(checks, 25);
}

TEST(CameraDrag, OldPressFoundTheGrabAndTheHoldWithTwoCameras)
{
	// Before the fix, the game could take the grab point with the camera that the user saw, and hold it with the ray
	// of its own camera through the same cursor. A press with no motion then moved the land.
	const WorldPoint focus {40.0f, k_LandLevel, 25.0f};
	const ShownCamera game {.stage = k_Stages[0], .tilt = 20.0f};
	const ShownCamera pitched {.stage = k_Stages[0], .tilt = 5.0f, .lookUp = 10.0f};
	const Vector2 cursor {960.0f, 200.0f};
	const auto grab = LandUnder(pitched.At(focus), cursor);
	const auto oldShift = GameHoldShift(ScreenRay(game.At(focus), cursor), grab);
	EXPECT_GT(Length(oldShift), 1.0f);
	ExpectNear(HoldShift(ScreenRay(pitched.At(focus), cursor), grab), {}, 2e-3f);
}

TEST(CameraDrag, HoldsTheGrabUnderTheCursorAcrossFrames)
{
	// The cursor moves over the lower part of the screen for 90 frames. Each frame, the mod moves the camera that the
	// user sees so that the grab point is under the cursor again. The clearance over the hills changes the camera
	// height between frames.
	for (const auto& shown : ShownCameras())
	{
		WorldPoint focus {40.0f, k_LandLevel, 25.0f};
		const Vector2 start {700.0f, 250.0f};
		const auto grab = LandUnder(shown.At(focus), start);
		float largestError = 0.0f;
		for (int frame = 1; frame <= 90; ++frame)
		{
			const Vector2 cursor {start.x + 6.0f * static_cast<float>(frame),
			                      start.y + 20.0f * std::sin(0.2f * static_cast<float>(frame))};
			auto camera = shown.At(focus);
			const auto shift = HoldShift(ScreenRay(camera, cursor), grab);
			focus = focus + shift;
			camera.position = camera.position + shift;
			largestError = std::max(largestError, ScreenError(camera, grab, cursor));
		}
		EXPECT_LT(largestError, 0.05f) << shown.stage.name << " tilt " << shown.tilt << " look up " << shown.lookUp;
	}
}

TEST(CameraDrag, OldLoopJumpsAndTheNewHoldDoesNot)
{
	// Before the fix: the game cast the ray of its drag from the camera of the last frame, which the mod had turned
	// up. Near the ground, with a flat tilt and a look up, the cursor moves up past the horizon. The ray of the game
	// then goes up, and the focus jumps the wrong way, far away.
	const ShownCamera shown {.stage = k_Stages[0], .tilt = 5.0f, .lookUp = 15.0f};
	const Vector2 start {960.0f, 120.0f};
	const WorldPoint startFocus {40.0f, k_LandLevel, 25.0f};
	auto oldFocus = startFocus;
	auto newFocus = startFocus;
	const auto grab = LandUnder(shown.At(startFocus), start);
	// A cursor that moves up pulls the land away from the camera, so the camera moves back.
	const auto forward = Rotate(YawRotation(30.0f), WorldPoint {0.0f, 0.0f, 1.0f});
	float oldWrongWay = 0.0f;
	float newWrongWay = 0.0f;
	float newLargestError = 0.0f;
	for (int frame = 1; frame <= 120; ++frame)
	{
		const Vector2 cursor {start.x, start.y + 5.0f * static_cast<float>(frame)};
		const auto oldShift = GameHoldShift(ScreenRay(shown.At(oldFocus), cursor), grab);
		oldWrongWay = std::max(oldWrongWay, Dot(oldShift, forward));
		oldFocus = oldFocus + oldShift;

		auto camera = shown.At(newFocus);
		const auto newShift = HoldShift(ScreenRay(camera, cursor), grab);
		newWrongWay = std::max(newWrongWay, Dot(newShift, forward));
		newFocus = newFocus + newShift;
		camera.position = camera.position + newShift;
		if (ScreenRay(camera, cursor).direction.y < -std::sin(2.0f * k_DegreesToRadians))
		{
			newLargestError = std::max(newLargestError, ScreenError(camera, grab, cursor));
		}
	}
	EXPECT_GT(oldWrongWay, 100.0f);
	EXPECT_LT(newWrongWay, 1e-2f);
	// With the cursor on the sky, the grab point stays at the largest distance of the limits.
	EXPECT_LT(Length(newFocus - startFocus), HoldLimits {}.maximumDistance + 20.0f);
	EXPECT_LT(newLargestError, 0.05f);
}

TEST(CameraDrag, KeepsThePointInFrontWhenTheCursorIsOnTheSky)
{
	const Ray down {.origin = {0.0f, 100.0f, 0.0f}, .direction = Normalize({0.0f, -1.0f, 1.0f})};
	ExpectNear(PointOnLevel(down, 90.0f), {0.0f, 90.0f, 10.0f}, 1e-3f);
	// A ray that goes up gives the point at the largest distance in front, not a point behind the camera.
	const Ray up {.origin = {0.0f, 100.0f, 0.0f}, .direction = Normalize({0.0f, 0.2f, 1.0f})};
	ExpectNear(PointOnLevel(up, 90.0f), {0.0f, 90.0f, 500.0f}, 1e-2f);
	// The point changes continuously as the ray goes over the largest distance (a descent of 1 in 50).
	const auto justBelow = PointOnLevel({.origin = up.origin, .direction = Normalize({0.0f, -0.0201f, 1.0f})}, 90.0f);
	const auto justAbove = PointOnLevel({.origin = up.origin, .direction = Normalize({0.0f, -0.0199f, 1.0f})}, 90.0f);
	EXPECT_LT(Length(justBelow - justAbove), 5.0f);
}

TEST(CameraDrag, FollowsTheGrabRulesOfTheGame)
{
	DragGrab drag;
	drag.idlePoint = WorldPoint {1.0f, 90.0f, 2.0f};
	// No hold: no grab point.
	EXPECT_FALSE(drag.Step({.holding = false, .gameGrab = {5.0f, 90.0f, 5.0f}}));
	// The hold starts with no press: the land under the cursor of the last frame.
	auto grab = drag.Step({.holding = true, .gameGrab = {5.0f, 90.0f, 5.0f}});
	ASSERT_TRUE(grab);
	ExpectNear(*grab, {1.0f, 90.0f, 2.0f});
	// The game adds a key pan to its grab point. The grab point of the user moves by the same pan.
	drag.gameGrabWritten = {5.0f, 90.0f, 5.0f};
	grab = drag.Step({.holding = true, .gameGrab = {6.5f, 90.0f, 4.0f}});
	ExpectNear(*grab, {2.5f, 90.0f, 1.0f});
	// A new press takes the land under the cursor at the frame start.
	grab = drag.Step(
	    {.holding = true, .grabbedAgain = true, .gameGrab = {0.0f, 0.0f, 0.0f}, .pressPoint = WorldPoint {7.0f, 91.0f, 8.0f}});
	ExpectNear(*grab, {7.0f, 91.0f, 8.0f});
	// A press with the cursor on the sky keeps the grab point.
	grab = drag.Step({.holding = true, .grabbedAgain = true, .gameGrab = {0.0f, 0.0f, 0.0f}});
	ExpectNear(*grab, {7.0f, 91.0f, 8.0f});
}

namespace
{

/// The camera of an in-place view: the rig, turned up by the part of the view above the rig.
[[nodiscard]] ScreenCamera InPlaceCamera(const InPlaceView& view)
{
	return {.position = RigPosition(view.rig), .rotation = Multiply(RigRotation(view.rig), LookUpRotation(view.lookUp))};
}

/// The angle of the view centre above the horizon, degrees.
[[nodiscard]] float ViewElevation(Quaternion rotation)
{
	return std::asin(std::clamp(Rotate(rotation, WorldPoint {0.0f, 0.0f, 1.0f}).y, -1.0f, 1.0f)) / k_DegreesToRadians;
}

} // namespace

TEST(CameraPitchInPlace, GivesTheCameraOfTheGameForNoRaise)
{
	// Also at Planning and Map, where the game puts the focus farther away than the smallest reach.
	for (const auto& [height, tilt] : {std::pair {6.0f, 20.0f}, std::pair {30.0f, 25.0f}, std::pair {90.0f, 45.0f},
	                                   std::pair {200.0f, 50.0f}, std::pair {1200.0f, 50.0f}})
	{
		const CameraRig game {
		    .focus = {10.0f, 92.0f, 20.0f}, .direction = 30.0f, .tilt = tilt, .distance = StageDistance(height, tilt)};
		const auto view = InPlaceRig(game.focus, game.direction, height, tilt, InPlaceFocusReach(height, tilt));
		EXPECT_NEAR(view.lookUp, 0.0f, 1e-4f);
		EXPECT_NEAR(view.rig.tilt, tilt, 1e-4f);
		ExpectNear(RigPosition(view.rig), RigPosition(game), 1e-2f);
	}
}

TEST(CameraPitchInPlace, KeepsTheCameraAtItsPlaceAcrossAPitchSweep)
{
	for (size_t index = 0; index < k_Stages.size(); ++index)
	{
		const auto& stage = k_Stages[index];
		SCOPED_TRACE(stage.name);
		const WorldPoint focus {40.0f, k_LandLevel, -15.0f};
		const auto range = PitchRangeFor(static_cast<float>(index), stage.tilt, k_Limits);
		const auto reach = InPlaceFocusReach(stage.height, stage.tilt);
		const auto start = InPlaceRig(focus, 30.0f, stage.height, stage.tilt, reach);
		const auto camera = RigPosition(start.rig);
		// Sweep from the steepest view to the highest view and back, in small smoothed frames.
		auto rig = start.rig;
		float raise = 0.0f;
		float previousElevation = ViewElevation(RigRotation(start.rig));
		float highest = previousElevation;
		float lowest = previousElevation;
		for (const auto target : {range.steepest, range.highest, 0.0f})
		{
			for (int frame = 0; frame < 240; ++frame)
			{
				const auto from = SplitPitch(raise, stage.tilt, range);
				const auto previousRaise = raise;
				raise = FollowValue(raise, target, 1.0f / 60.0f, 0.3f);
				const auto to = SplitPitch(raise, stage.tilt, range);
				const auto view =
				    TurnInPlace(rig.focus, rig.direction, stage.height, from.tilt - from.lookUp, to.tilt - to.lookUp, reach);
				rig = view.rig;
				// The camera does not move: not horizontally, and not up or down.
				ExpectNear(RigPosition(rig), camera, 2e-2f);
				// The focus stays at the height of the land, and the view centre is the view of the raise.
				EXPECT_FLOAT_EQ(rig.focus.y, focus.y);
				const auto rotation = InPlaceCamera(view).rotation;
				const auto elevation = ViewElevation(rotation);
				EXPECT_NEAR(elevation, to.lookUp - to.tilt, 1e-2f);
				// The view changes by the change of the raise and no more, also across the join with the look up.
				EXPECT_LE(std::abs(elevation - previousElevation), std::abs(raise - previousRaise) + 1e-2f);
				previousElevation = elevation;
				highest = std::max(highest, elevation);
				lowest = std::min(lowest, elevation);
				if (view.lookUp <= 0.0f)
				{
					// The focus is where the view centre meets the land.
					const auto forward = Rotate(rotation, WorldPoint {0.0f, 0.0f, 1.0f});
					ExpectNear(camera + forward * rig.distance, rig.focus, 2e-2f);
				}
				else
				{
					// The view centre does not meet the land in the reach: the focus is at the reach.
					EXPECT_NEAR(std::hypot(rig.focus.x - camera.x, rig.focus.z - camera.z), reach, 0.5f);
				}
			}
		}
		// The sweep got to the limits of the tilt: 85 and 5 degrees, and the look up only near the ground.
		EXPECT_NEAR(lowest, -85.0f, 0.1f);
		EXPECT_NEAR(highest, index == 0 ? 15.0f : (index == 1 ? 0.5f * (5.0f + 15.0f) - 5.0f : -5.0f), 0.1f);
		// The raise went back to zero: the view and the camera of the game.
		EXPECT_NEAR(rig.tilt, stage.tilt, 1e-2f);
		ExpectNear(rig.focus, focus, 5e-2f);
	}
}

TEST(CameraPitchInPlace, ZoomsAlongTheViewDirection)
{
	// A pitched view keeps its focus when the game changes the height. With the same tilt, the camera moves along
	// the line from the focus to the camera.
	const WorldPoint focus {0.0f, k_LandLevel, 0.0f};
	const auto near = InPlaceRig(focus, -60.0f, 6.0f, 10.0f, InPlaceFocusReach(6.0f, 20.0f)).rig;
	const auto far = InPlaceRig(focus, -60.0f, 12.0f, 10.0f, InPlaceFocusReach(12.0f, 20.0f)).rig;
	const auto line = Normalize(RigPosition(near) - focus);
	const auto move = RigPosition(far) - RigPosition(near);
	ExpectNear(Cross(line, move), {}, 1e-3f);
	EXPECT_GT(Dot(line, move), 0.0f);
	ExpectNear(far.focus, focus);
}

TEST(CameraInertia, FindsTheSpeedFromFramesWithNoise)
{
	// A drag at 12 m/s along x and 5 m/s along z. The mouse gives no move in some frames, as a mouse with fewer
	// updates than frames does.
	const WorldPoint speed {12.0f, 0.0f, 5.0f};
	for (const auto rate : {30, 60, 144})
	{
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		VelocityFilter filter;
		WorldPoint point {};
		float carried = 0.0f;
		for (int frame = 0; frame < rate; ++frame)
		{
			carried += deltaTime;
			if (frame % 7 != 3)
			{
				point = point + speed * carried;
				carried = 0.0f;
			}
			filter.Add(point, deltaTime, 0.1f);
		}
		EXPECT_NEAR(filter.velocity.x, speed.x, 0.15f * speed.x) << rate;
		EXPECT_NEAR(filter.velocity.z, speed.z, 0.15f * speed.z) << rate;
		EXPECT_FLOAT_EQ(filter.velocity.y, 0.0f);
		// One last frame with a jump of 5 cm does not control the result.
		auto jumped = filter;
		jumped.Add(point + speed * deltaTime + WorldPoint {0.05f, 0.0f, 0.0f}, deltaTime, 0.1f);
		EXPECT_LT(std::abs(jumped.velocity.x - filter.velocity.x), 0.05f / 0.1f + 0.2f * speed.x) << rate;
	}
	// A short drag gives its real speed at once.
	VelocityFilter quick;
	quick.Add({}, 1.0f / 60.0f, 0.1f);
	EXPECT_FALSE(quick.HasVelocity());
	quick.Add({0.2f, 0.0f, 0.0f}, 1.0f / 60.0f, 0.1f);
	ASSERT_TRUE(quick.HasVelocity());
	EXPECT_NEAR(quick.velocity.x, 12.0f, 1e-3f);
}

TEST(CameraInertia, GlidesTheSameDistanceAtEachFrameRate)
{
	const WorldPoint start {20.0f, 0.0f, -10.0f};
	const float timeConstant = 0.35f;
	const float stopSpeed = 0.06f;
	std::vector<float> distances;
	for (const auto rate : {30, 60, 144})
	{
		Glide glide;
		ASSERT_TRUE(glide.Start(start, 1000.0f, stopSpeed));
		WorldPoint travel {};
		int frames = 0;
		while (glide.active && frames < 10000)
		{
			travel = travel + glide.Step(1.0f / static_cast<float>(rate), timeConstant, stopSpeed);
			++frames;
		}
		EXPECT_FALSE(glide.active);
		EXPECT_FLOAT_EQ(travel.y, 0.0f);
		// The direction of the glide is the direction of the drag.
		EXPECT_NEAR(travel.x / travel.z, start.x / start.z, 1e-3f);
		distances.push_back(Length(travel));
	}
	// The full glide is the start speed times the time constant.
	EXPECT_NEAR(distances[0], Length(start) * timeConstant, 0.01f * Length(start) * timeConstant);
	EXPECT_NEAR(distances[1], distances[0], 0.01f * distances[0]);
	EXPECT_NEAR(distances[2], distances[0], 0.01f * distances[0]);
	// The speed decreases with no step: half of the glide is done after ln(2) times the time constant.
	Glide half;
	ASSERT_TRUE(half.Start(start, 1000.0f, stopSpeed));
	const auto move = half.Step(std::log(2.0f) * timeConstant, timeConstant, stopSpeed);
	EXPECT_NEAR(Length(move), 0.5f * Length(start) * timeConstant, 1e-3f);
	EXPECT_NEAR(Length(half.velocity), 0.5f * Length(start), 1e-3f);
}

TEST(CameraInertia, StopsOnAPressAndKeepsTheLimits)
{
	Glide glide;
	ASSERT_TRUE(glide.Start({300.0f, 0.0f, 400.0f}, 100.0f, 0.1f));
	// The speed is never more than the limit, in the same direction.
	EXPECT_NEAR(Length(glide.velocity), 100.0f, 1e-3f);
	EXPECT_NEAR(glide.velocity.x / glide.velocity.z, 0.75f, 1e-4f);
	(void)glide.Step(1.0f / 60.0f, 0.35f, 0.1f);
	EXPECT_TRUE(glide.active);
	// A press stops the glide at once.
	glide.Stop();
	ExpectNear(glide.Step(1.0f / 60.0f, 0.35f, 0.1f), {});
	EXPECT_FALSE(glide.active);
	// A drag that stopped before the release gives no glide.
	EXPECT_FALSE(glide.Start({0.05f, 0.0f, 0.0f}, 100.0f, 0.1f));
	// A time constant of zero turns the glide off.
	ASSERT_TRUE(glide.Start({10.0f, 0.0f, 0.0f}, 100.0f, 0.1f));
	ExpectNear(glide.Step(1.0f / 60.0f, 0.0f, 0.1f), {});
	EXPECT_FALSE(glide.active);
	// The smoothed cursor of the game moves one tenth of the way in a frame at 60 frames for each second.
	EXPECT_NEAR(GameDragCursorStep(1.0f / 60.0f), 0.1f, 1e-5f);
}

TEST(UnityMath, TurnsAboutYLikeTurnAboutPivot)
{
	const WorldPoint point {3.0f, 2.0f, -7.0f};
	ExpectNear(Rotate(YawRotation(37.0f), point), TurnAboutPivot(point, {}, 37.0f));
}
