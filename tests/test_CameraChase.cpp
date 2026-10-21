/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <cmath>

#include <algorithm>
#include <array>
#include <vector>

#include <core/CameraChase.h>
#include <gtest/gtest.h>

#include "CameraLoopHarness.h"

namespace
{

using moa_mod::Length;
using moa_mod::Vector2;
using moa_mod::WorldPoint;
using moa_mod::WrapDegrees;
using moa_mod::camera::ChaseSpring;
using moa_mod::camera::GripLandMove;
using moa_mod::camera::GripLimits;
using moa_mod::camera::GripPlaneThrough;
using moa_mod::camera::InputSmoother;
using moa_mod::camera::PitchMode;
using moa_mod::camera::ScreenCamera;
using moa_mod::camera::StopBeforeLand;
using moa_mod::camera::test::CameraLoopHarness;

constexpr std::array k_Rates {30, 60, 144};
/// A sixth of a second is a whole count of frames at 30, 60 and 144 frames for each second.
constexpr float k_CommonStep = 1.0f / 6.0f;

/// The path of a camera that chases a target. The target moves at `speed` for `moveSeconds`, then stops. Return the
/// camera at each sixth of a second, for `seconds`.
[[nodiscard]] std::vector<WorldPoint> ChasePath(int rate, WorldPoint speed, float moveSeconds, float seconds,
                                                float chaseSeconds, float maximumSpeed = 0.0f)
{
	const auto deltaTime = 1.0f / static_cast<float>(rate);
	const auto framesPerSample = static_cast<int>(std::lround(k_CommonStep * static_cast<float>(rate)));
	const auto moveFrames = static_cast<int>(std::lround(moveSeconds * static_cast<float>(rate)));
	const auto frames = static_cast<int>(std::lround(seconds * static_cast<float>(rate)));
	ChaseSpring spring;
	WorldPoint target;
	std::vector<WorldPoint> path;
	for (int frame = 1; frame <= frames; ++frame)
	{
		const auto move = frame <= moveFrames ? speed * deltaTime : WorldPoint {};
		target = target + move;
		spring.Step(move, deltaTime, chaseSeconds, maximumSpeed);
		if (frame % framesPerSample == 0)
		{
			path.push_back(target + spring.lag);
		}
	}
	return path;
}

TEST(ChaseSpring, GivesTheSamePathAtEachFrameRate)
{
	const WorldPoint speed {40.0f, 0.0f, -25.0f};
	const auto reference = ChasePath(144, speed, 1.0f, 3.0f, 0.3f);
	for (const auto rate : k_Rates)
	{
		const auto path = ChasePath(rate, speed, 1.0f, 3.0f, 0.3f);
		ASSERT_EQ(path.size(), reference.size());
		for (size_t index = 0; index < path.size(); ++index)
		{
			EXPECT_LT(Length(path[index] - reference[index]), 0.01f) << rate << " fps, sample " << index;
		}
	}
	// At the end the camera is on the target.
	EXPECT_LT(Length(reference.back() - speed * 1.0f), 0.01f);
}

TEST(ChaseSpring, KeepsTheSpeedFromFrameToFrame)
{
	for (const auto rate : k_Rates)
	{
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		ChaseSpring spring;
		const WorldPoint speed {30.0f, 0.0f, 0.0f};
		float lastSpeed = 0.0f;
		float largestChange = 0.0f;
		for (int frame = 0; frame < rate * 2; ++frame)
		{
			// The target starts at once, and stops at once after one second.
			const auto move = frame < rate ? speed * deltaTime : WorldPoint {};
			spring.Step(move, deltaTime, 0.3f);
			const auto now = spring.velocity.x;
			largestChange = std::max(largestChange, std::abs(now - lastSpeed) / deltaTime);
			lastSpeed = now;
		}
		// A sudden start or stop of the target never changes the speed of the camera at once: the change for each
		// second stays below the start speed times the rate of the spring.
		EXPECT_LT(largestChange, 30.0f * (moa_mod::camera::k_ChaseRate / 0.3f)) << rate;
		// In the first frame of the move the camera speed is only a part of the target speed.
		ChaseSpring first;
		first.Step(speed * deltaTime, deltaTime, 0.3f);
		EXPECT_LT(first.velocity.x, 30.0f * 0.75f) << rate;
	}
}

TEST(ChaseSpring, DoesNotGoPastATargetThatStops)
{
	for (const auto rate : k_Rates)
	{
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		ChaseSpring spring;
		for (int frame = 0; frame < rate * 4; ++frame)
		{
			const auto move = frame < rate ? 50.0f * deltaTime : 0.0f;
			spring.Step({move, 0.0f, 0.0f}, deltaTime, 0.3f);
			// The camera is never in front of the target.
			EXPECT_LE(spring.lag.x, 1e-4f) << rate << " fps, frame " << frame;
		}
		EXPECT_TRUE(spring.Still()) << rate;
	}
	// A sudden jump of the target from rest: the camera arrives with no overshoot.
	ChaseSpring spring;
	spring.Step({100.0f, 0.0f, 0.0f}, 0.0f, 0.3f);
	EXPECT_FLOAT_EQ(spring.lag.x, -100.0f);
	for (int frame = 0; frame < 120; ++frame)
	{
		spring.Step({}, 1.0f / 60.0f, 0.3f);
		EXPECT_LE(spring.lag.x, 1e-4f);
	}
}

TEST(ChaseSpring, CoversMostOfASuddenMoveInItsTime)
{
	ChaseSpring spring;
	spring.Step({10.0f, 0.0f, 0.0f}, 0.0f, 0.3f);
	for (int frame = 0; frame < 18; ++frame)
	{
		spring.Step({}, 1.0f / 60.0f, 0.3f);
	}
	// After 0.3 s, 95 percent of the move is done.
	EXPECT_NEAR(spring.lag.x, -0.5f, 0.02f);
}

TEST(ChaseSpring, NeverMovesFasterThanTheLimit)
{
	for (const auto rate : k_Rates)
	{
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		ChaseSpring spring;
		// A leap of the target of 2 km.
		spring.Step({2000.0f, 0.0f, 0.0f}, deltaTime, 0.3f, 150.0f);
		float travelled = 2000.0f + spring.lag.x;
		EXPECT_LE(travelled, 150.0f * deltaTime + 1e-3f) << rate;
		for (int frame = 0; frame < rate; ++frame)
		{
			const auto before = spring.lag.x;
			spring.Step({}, deltaTime, 0.3f, 150.0f);
			EXPECT_LE(spring.lag.x - before, 150.0f * deltaTime + 1e-3f) << rate;
			EXPECT_LE(Length(spring.velocity), 150.0f + 1e-3f);
		}
	}
}

TEST(ChaseSpring, IsOffWithATimeOfZero)
{
	ChaseSpring spring;
	spring.Step({5.0f, 1.0f, 2.0f}, 1.0f / 60.0f, 0.0f);
	EXPECT_TRUE(Length(spring.lag) == 0.0f);
	EXPECT_NEAR(spring.velocity.x, 300.0f, 1e-3f);
}

TEST(InputSmoother, GivesAllTheInputAndTheSameAtEachFrameRate)
{
	std::vector<std::vector<float>> outputs;
	for (const auto rate : k_Rates)
	{
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		const auto framesPerSample = static_cast<int>(std::lround(k_CommonStep * static_cast<float>(rate)));
		InputSmoother smoother;
		float total = 0.0f;
		std::vector<float> samples;
		for (int frame = 1; frame <= rate * 2; ++frame)
		{
			// 90 degrees for each second for half a second, then nothing.
			const auto input = frame <= rate / 2 ? 90.0f * deltaTime : 0.0f;
			total += smoother.Step(input, deltaTime, 0.03f);
			if (frame % framesPerSample == 0)
			{
				samples.push_back(total);
			}
		}
		EXPECT_NEAR(total, 45.0f, 1e-3f) << rate;
		outputs.push_back(samples);
	}
	for (size_t rate = 0; rate < outputs.size(); ++rate)
	{
		for (size_t index = 0; index < outputs[rate].size(); ++index)
		{
			EXPECT_NEAR(outputs[rate][index], outputs.back()[index], 1e-3f) << k_Rates[rate] << " fps, sample " << index;
		}
	}
}

TEST(InputSmoother, DelaysTheInputByItsTimeConstant)
{
	InputSmoother smoother;
	float total = 0.0f;
	for (int frame = 0; frame < 60; ++frame)
	{
		total += smoother.Step(90.0f / 60.0f, 1.0f / 60.0f, 0.03f);
	}
	// In a steady move, the output is behind the input by the rate times the time constant.
	EXPECT_NEAR(90.0f - total, 90.0f * 0.03f, 0.01f);
	InputSmoother off;
	EXPECT_FLOAT_EQ(off.Step(3.0f, 1.0f / 60.0f, 0.0f), 3.0f);
}

TEST(GripPlane, IsLevelUnlessTheGripIsHigherThanTheLandUnderTheCamera)
{
	const WorldPoint camera {0.0f, 130.0f, 0.0f};
	const auto level = GripPlaneThrough({0.0f, 100.0f, 50.0f}, camera, 100.0f);
	EXPECT_NEAR(level.normal.y, 1.0f, 1e-6f);
	EXPECT_NEAR(level.offset, 100.0f, 1e-4f);
	// A grip that is lower than the land under the camera also gives a level plane.
	const auto lower = GripPlaneThrough({0.0f, 90.0f, 50.0f}, camera, 100.0f);
	EXPECT_NEAR(lower.normal.y, 1.0f, 1e-6f);
	// A grip on a hill: the plane rises to the grip.
	const auto hill = GripPlaneThrough({0.0f, 120.0f, 20.0f}, camera, 100.0f);
	EXPECT_LT(hill.normal.y, 0.99f);
	EXPECT_LT(hill.normal.z, 0.0f);
	EXPECT_NEAR(Length(hill.normal), 1.0f, 1e-5f);
	EXPECT_NEAR(moa_mod::Dot(hill.normal, WorldPoint {0.0f, 100.0f, 0.0f}), hill.offset, 1e-3f);
}

/// A camera 20 m above flat land, with a tilt of 10 degrees: the top of the screen is near the horizon.
[[nodiscard]] ScreenCamera FlatViewCamera()
{
	return {.position = {0.0f, 120.0f, 0.0f},
	        .rotation = moa_mod::camera::HandRigRotation(0.0f, 10.0f),
	        .verticalFieldOfView = 60.0f,
	        .width = 1920.0f,
	        .height = 1080.0f};
}

TEST(GripLandMove, KeepsTheLandUnderTheCursorAndLimitsTheMoveNearTheHorizon)
{
	const auto camera = FlatViewCamera();
	const auto plane = GripPlaneThrough({0.0f, 100.0f, 0.0f}, camera.position, 100.0f);
	const GripLimits limits {.stepPerPixel = 0.011f, .minimumStep = 1.0f};
	const auto ray = [&](float x, float y) { return moa_mod::camera::ScreenRay(camera, Vector2 {x, y}).direction; };
	// No move of the cursor: no move of the land.
	auto none = GripLandMove(plane, camera.position, ray(960.0f, 300.0f), ray(960.0f, 300.0f), 0.0f, 60.0f, limits);
	ASSERT_TRUE(none);
	EXPECT_LT(Length(*none), 1e-4f);
	// Near the screen centre the move is the move of the land under the cursor, with no limit.
	const auto atGrip = ray(960.0f, 300.0f);
	const auto now = ray(1000.0f, 330.0f);
	auto centre = GripLandMove(plane, camera.position, now, atGrip, std::hypot(40.0f, 30.0f), 60.0f, limits);
	ASSERT_TRUE(centre);
	const auto pointAt = [&](WorldPoint direction) { return camera.position + direction * (-20.0f / direction.y); };
	EXPECT_LT(Length(*centre - (pointAt(now) - pointAt(atGrip))), 1e-3f);
	// Near the horizon (at 705 pixels for a tilt of 10 degrees), one pixel moves the land at most 0.011 of the
	// distance, but the minimum step.
	const auto high = ray(960.0f, 700.0f);
	const auto higher = ray(960.0f, 701.0f);
	const auto distance = Length(pointAt(high) - camera.position);
	auto horizon = GripLandMove(plane, camera.position, higher, high, 1.0f, distance, limits);
	ASSERT_TRUE(horizon);
	EXPECT_LE(Length(*horizon), std::max(1.0f, 0.011f * distance) + 1e-3f);
	EXPECT_GT(Length(pointAt(higher) - pointAt(high)), std::max(1.0f, 0.011f * distance));
	// A cursor above the horizon gives no move.
	EXPECT_FALSE(GripLandMove(plane, camera.position, ray(960.0f, 1000.0f), high, 300.0f, distance, limits));
}

TEST(StopBeforeLand, StopsTheMoveBeforeAHill)
{
	// A hill of 30 m at x from 40 to 60.
	const auto land = [](WorldPoint point) -> std::optional<float> {
		return point.x > 40.0f && point.x < 60.0f ? 130.0f : 100.0f;
	};
	auto end = StopBeforeLand({0.0f, 120.0f, 0.0f}, {100.0f, 120.0f, 0.0f}, 3.0f, land);
	ASSERT_TRUE(end);
	EXPECT_LT(end->x, 40.0f);
	EXPECT_GT(end->x, 30.0f);
	auto clear = StopBeforeLand({0.0f, 140.0f, 0.0f}, {100.0f, 140.0f, 0.0f}, 3.0f, land);
	ASSERT_TRUE(clear);
	EXPECT_FLOAT_EQ(clear->x, 100.0f);
}

// The hand rig.

[[nodiscard]] moa_mod::camera::CameraSettings RigSettings()
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

TEST(LandGrip, HoldsTheGrippedLandUnderTheCursor)
{
	for (const auto rate : k_Rates)
	{
		SCOPED_TRACE(std::to_string(rate) + " fps");
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		auto settings = RigSettings();
		settings.dragInertia = 0.0;
		auto harness = Settled(settings, 1.0f, deltaTime);
		auto& game = harness.game;
		game.mouse = {900.0f, 500.0f, 0.0f};
		harness.Frame(deltaTime);
		const auto grip = game.LandAt(game.transform, game.mouse);
		ASSERT_TRUE(grip);
		game.buttons[1] = true;
		harness.Frame(deltaTime);
		for (int frame = 0; frame < rate / 2; ++frame)
		{
			game.mouse.x += 300.0f * deltaTime;
			game.mouse.y -= 120.0f * deltaTime;
			harness.Frame(deltaTime);
			EXPECT_TRUE(harness.loop.Trace().gripActive);
		}
		RunFor(harness, 1.0f, deltaTime);
		// The camera chased the target: the gripped land is under the cursor again.
		const auto under = game.LandAt(game.transform, game.mouse);
		ASSERT_TRUE(under);
		EXPECT_LT(std::hypot(under->x - grip->x, under->z - grip->z), 0.05f);
		game.buttons[1] = false;
		harness.Frame(deltaTime);
		EXPECT_TRUE(game.errors.empty());
	}
}

TEST(LandGrip, HoldsTheGrippedLandAfterAPitch)
{
	const auto deltaTime = 1.0f / 60.0f;
	auto settings = RigSettings();
	settings.pitch = true;
	settings.dragInertia = 0.0;
	auto harness = Settled(settings, 0.5f, deltaTime);
	auto& game = harness.game;
	game.ground = [](float x, float z) { return 100.0f + 12.0f * std::sin(x / 37.0f) * std::cos(z / 53.0f); };
	RunFor(harness, 0.5f, deltaTime);
	// A pitch with the middle button.
	game.buttons[2] = true;
	harness.Frame(deltaTime);
	for (int frame = 0; frame < 20; ++frame)
	{
		game.mouse.y -= 2.0f;
		harness.Frame(deltaTime);
	}
	game.buttons[2] = false;
	RunFor(harness, 1.0f, deltaTime);
	const auto grip = game.LandAt(game.transform, game.mouse);
	ASSERT_TRUE(grip);
	game.buttons[1] = true;
	harness.Frame(deltaTime);
	for (int frame = 0; frame < 36; ++frame)
	{
		game.mouse.y += 200.0f * deltaTime;
		game.mouse.x -= 150.0f * deltaTime;
		harness.Frame(deltaTime);
	}
	RunFor(harness, 1.0f, deltaTime);
	const auto under = game.LandAt(game.transform, game.mouse);
	ASSERT_TRUE(under);
	// The land drag holds the point on a plane through it; on hills the land under the cursor is near that point.
	EXPECT_LT(std::hypot(under->x - grip->x, under->z - grip->z), 1.0f);
	EXPECT_TRUE(game.errors.empty());
}

TEST(LandGrip, DoesNotLeapNearTheHorizon)
{
	for (const auto rate : k_Rates)
	{
		SCOPED_TRACE(std::to_string(rate) + " fps");
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		auto settings = RigSettings();
		settings.dragInertia = 0.0;
		auto harness = Settled(settings, 0.0f, deltaTime);
		auto& game = harness.game;
		// The cursor just under the horizon at Detail (at 880 pixels for a tilt of 20 degrees).
		game.mouse = {960.0f, 860.0f, 0.0f};
		harness.Frame(deltaTime);
		const auto grip = game.LandAt(game.transform, game.mouse);
		ASSERT_TRUE(grip);
		// The limit for each pixel is a part of the distance from the camera to the focus.
		const auto distance = Length(game.focus - game.transform.position);
		game.buttons[1] = true;
		harness.Frame(deltaTime);
		const auto start = harness.Shown();
		float largestTarget = 0.0f;
		float largestShownStep = 0.0f;
		// A drag up, to the horizon and past it.
		for (int frame = 0; frame < rate / 2; ++frame)
		{
			game.mouse.y += 240.0f * deltaTime;
			const auto before = harness.Shown();
			harness.Frame(deltaTime);
			largestShownStep = std::max(largestShownStep, Length(harness.Shown() - before));
			if (const auto& move = harness.loop.Trace().gripMove)
			{
				largestTarget = std::max(largestTarget, Length(*move));
			}
		}
		const auto pixels = game.mouse.y - 860.0f;
		RunFor(harness, 1.0f, deltaTime);
		// The whole move is never more than the limit for the move of the cursor.
		EXPECT_LE(Length(harness.Shown() - start), std::max(1.0f, 0.011f * distance * pixels) + 0.5f);
		// The camera that the user sees never moves faster than the chase permits.
		EXPECT_LE(largestShownStep / deltaTime, 6.0f * 200.0f + 1.0f);
		EXPECT_GT(largestTarget, 0.0f);
		game.buttons[1] = false;
		EXPECT_TRUE(game.errors.empty());
	}
}

/// The move of the camera that the user sees along x, for each frame of a key pan of one second and a wait.
[[nodiscard]] std::vector<float> KeyPanSpeeds(int rate, double chaseSeconds)
{
	const auto deltaTime = 1.0f / static_cast<float>(rate);
	auto settings = RigSettings();
	settings.chaseSeconds = chaseSeconds;
	auto harness = Settled(settings, 1.0f, deltaTime);
	auto& game = harness.game;
	game.direction = 90.0f;
	RunFor(harness, 0.5f, deltaTime);
	std::vector<float> speeds;
	for (int frame = 0; frame < rate * 3; ++frame)
	{
		game.panKeys = {.x = 0.0f, .y = frame < rate ? 1.0f : 0.0f};
		const auto before = harness.Shown();
		harness.Frame(deltaTime);
		speeds.push_back((harness.Shown().x - before.x) / deltaTime);
	}
	EXPECT_TRUE(game.errors.empty());
	return speeds;
}

TEST(KeyPanChase, StartsAndStopsSmoothly)
{
	for (const auto rate : k_Rates)
	{
		SCOPED_TRACE(std::to_string(rate) + " fps");
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		const auto chased = KeyPanSpeeds(rate, 0.3);
		const auto plain = KeyPanSpeeds(rate, 0.0);
		const auto largestChange = [&](const std::vector<float>& speeds) {
			float largest = 0.0f;
			for (size_t index = 1; index < speeds.size(); ++index)
			{
				largest = std::max(largest, std::abs(speeds[index] - speeds[index - 1]) / deltaTime);
			}
			return largest;
		};
		// The chase makes the change of the speed smaller.
		EXPECT_LT(largestChange(chased), largestChange(plain));
		// The camera never moves back when the keys stop.
		EXPECT_GE(*std::ranges::min_element(chased), -1e-3f);
		// The camera stops.
		EXPECT_LT(std::abs(chased.back()), 0.02f);
		// The pan goes the same distance with and without the chase.
		float chasedPath = 0.0f;
		float plainPath = 0.0f;
		for (size_t index = 0; index < chased.size(); ++index)
		{
			chasedPath += chased[index] * deltaTime;
			plainPath += plain[index] * deltaTime;
		}
		EXPECT_NEAR(chasedPath, plainPath, 0.01f * plainPath + 0.01f);
	}
}

TEST(KeyPanChase, TheCameraOfTheChaseStaysAboveTheLandOnASlope)
{
	for (const auto rate : k_Rates)
	{
		SCOPED_TRACE(std::to_string(rate) + " fps");
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		// A ground clearance that is more than the height of the camera: the target is under its floor, as in the game
		// where the camera ball keeps the camera nearer to the land than the clearance.
		auto settings = RigSettings();
		settings.groundClearance = 8.0;
		auto harness = Settled(settings, 0.0f, deltaTime);
		auto& game = harness.game;
		// A steep slope ahead of the camera: the camera behind the target in the chase is over higher land.
		game.ground = [](float x, float) { return 100.0f + std::max(x - 10.0f, 0.0f) * 3.0f; };
		game.direction = 90.0f;
		RunFor(harness, 1.0f, deltaTime);
		float lowest = 1e9f;
		for (int frame = 0; frame < rate * 2; ++frame)
		{
			game.panKeys = {.x = 0.0f, .y = frame < rate ? 1.0f : 0.0f};
			harness.Frame(deltaTime);
			const auto shown = harness.Shown();
			lowest = std::min(lowest, shown.y - game.GroundAt(shown.x, shown.z));
		}
		EXPECT_GE(lowest, -1e-3f);
		EXPECT_TRUE(game.errors.empty());
	}
}

TEST(KeyPanChase, APanIntoACliffDoesNotThrowTheCamera)
{
	for (const auto rate : k_Rates)
	{
		SCOPED_TRACE(std::to_string(rate) + " fps");
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		auto harness = Settled(RigSettings(), 1.0f, deltaTime);
		auto& game = harness.game;
		// A cliff of 40 m ahead of the camera, as in the game where a key pan went into the cliffs of the coast.
		game.ground = [](float, float z) {
			const auto t = std::clamp((z - 40.0f) / 8.0f, 0.0f, 1.0f);
			return 100.0f + 40.0f * t * t * (3.0f - 2.0f * t);
		};
		game.direction = 0.0f;
		// A turn takes a pivot and makes the arm; the pan then carries the arm pivot to the cliff.
		game.turnPerSecond = 30.0f;
		RunFor(harness, 0.2f, deltaTime);
		game.turnPerSecond = 0.0f;
		RunFor(harness, 0.5f, deltaTime);
		float largestStep = 0.0f;
		float lowest = 1e9f;
		float deepestPivot = -1e9f;
		for (int frame = 0; frame < rate * 3; ++frame)
		{
			game.panKeys = {.x = 0.0f, .y = frame < rate * 2 ? 1.0f : 0.0f};
			const auto before = harness.Shown();
			harness.Frame(deltaTime);
			const auto shown = harness.Shown();
			largestStep = std::max(largestStep, Length(shown - before));
			lowest = std::min(lowest, shown.y - game.GroundAt(shown.x, shown.z));
			// The pan carries the pivot of the spring arm with the camera; it never goes under the cliff.
			if (const auto pivot = harness.loop.State()["armPivot"]; pivot.is_array())
			{
				const auto x = pivot[0].get<float>();
				const auto z = pivot[2].get<float>();
				deepestPivot = std::max(deepestPivot, game.GroundAt(x, z) - pivot[1].get<float>());
			}
		}
		// The pan moves at most about 1 m in a frame at 30 fps; a throw of the spring arm moves tens of metres.
		EXPECT_LT(largestStep, 5.0f);
		EXPECT_GE(lowest, -1e-3f);
		EXPECT_LT(deepestPivot, 0.1f);
		EXPECT_TRUE(game.errors.empty());
	}
}

/// A ridge 25 metres high between the view centre and the camera at Production, for x more than 20 metres.
[[nodiscard]] float RidgeLand(float x, float z)
{
	const auto along = std::clamp((x - 20.0f) / 8.0f, 0.0f, 1.0f);
	const auto across = std::clamp(1.0f - std::abs(z + 30.0f) / 6.0f, 0.0f, 1.0f);
	return 100.0f + 25.0f * along * across;
}

TEST(SpringArm, APanAcrossARidgeBetweenThePivotAndTheCameraDoesNotPullTheCameraIn)
{
	for (const auto rate : k_Rates)
	{
		SCOPED_TRACE(std::to_string(rate) + " fps");
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		auto harness = Settled(RigSettings(), 1.0f, deltaTime);
		auto& game = harness.game;
		game.ground = RidgeLand;
		game.direction = 0.0f;
		RunFor(harness, 0.5f, deltaTime);
		float largestStep = 0.0f;
		float lowest = 1e9f;
		float shortest = 1e9f;
		for (int frame = 0; frame < rate * 3; ++frame)
		{
			// The key pan to the right takes the line from the view centre to the camera across the ridge.
			game.panKeys = {.x = frame < rate * 2 ? 1.0f : 0.0f, .y = 0.0f};
			const auto before = harness.Shown();
			harness.Frame(deltaTime);
			const auto shown = harness.Shown();
			largestStep = std::max(largestStep, Length(shown - before));
			lowest = std::min(lowest, shown.y - game.GroundAt(shown.x, shown.z));
			const auto& trace = harness.loop.Trace();
			if (trace.armDistance && trace.desiredDistance)
			{
				shortest = std::min(shortest, *trace.armDistance / *trace.desiredDistance);
			}
		}
		// The pan moves about 1 m in a frame at 60 fps. The arm keeps its length: no line test with no gesture.
		EXPECT_LT(largestStep, 70.0f * deltaTime * 2.0f);
		EXPECT_GE(lowest, -1e-3f);
		EXPECT_GT(shortest, 0.99f);
		EXPECT_TRUE(game.errors.empty());
	}
}

TEST(SpringArm, ATurnThatPutsARidgeBetweenThePivotAndTheCameraPullsTheCameraInSmoothly)
{
	for (const auto rate : k_Rates)
	{
		SCOPED_TRACE(std::to_string(rate) + " fps");
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		auto settings = RigSettings();
		settings.turnInertia = 0.0;
		auto harness = Settled(settings, 1.0f, deltaTime);
		auto& game = harness.game;
		// A ridge 50 m high on a quarter of a ring 30 to 50 m from the view centre: a turn brings it between the pivot and
		// the camera.
		game.ground = [](float x, float z) {
			const auto radius = std::hypot(x, z);
			const auto ring = std::clamp(1.0f - std::abs(radius - 40.0f) / 10.0f, 0.0f, 1.0f);
			return x > 0.0f && z < 0.0f ? 100.0f + 50.0f * ring : 100.0f;
		};
		game.direction = 270.0f;
		RunFor(harness, 0.5f, deltaTime);
		float largestStep = 0.0f;
		float lowest = 1e9f;
		float shortest = 1e9f;
		game.buttons[2] = true;
		for (int frame = 0; frame < rate * 2; ++frame)
		{
			game.turnPerSecond = 60.0f;
			const auto before = harness.Shown();
			harness.Frame(deltaTime);
			const auto shown = harness.Shown();
			largestStep = std::max(largestStep, Length(shown - before));
			lowest = std::min(lowest, shown.y - game.GroundAt(shown.x, shown.z));
			const auto& trace = harness.loop.Trace();
			if (trace.armDistance && trace.desiredDistance)
			{
				shortest = std::min(shortest, *trace.armDistance / *trace.desiredDistance);
			}
		}
		game.buttons[2] = false;
		game.turnPerSecond = 0.0f;
		// In the gesture the ridge pulls the arm in, but never faster than 150 m/s (half the desired distance for each
		// second is less here), with the turn of about 75 m/s on top.
		EXPECT_LT(shortest, 0.99f);
		EXPECT_LT(largestStep, (150.0f + 80.0f) * deltaTime);
		EXPECT_GE(lowest, -1e-3f);
		EXPECT_TRUE(game.errors.empty());
	}
}

TEST(MapEdge, AZoomAtTheEdgeOfTheMapNeverThrowsTheCameraOrPutsItUnderTheLand)
{
	// In the game a zoom at the map edge moved the camera 51 m in one frame, 2.8 m under the land, when the clamp moved
	// the rig.
	for (const auto rate : k_Rates)
	{
		SCOPED_TRACE(std::to_string(rate) + " fps");
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		CameraLoopHarness harness(RigSettings());
		auto& game = harness.game;
		game.mapMinimum = -3000.0f;
		game.mapMaximum = 40.0f;
		game.ground = [](float x, float z) { return 100.0f + 20.0f * std::sin(x / 23.0f) * std::cos(z / 31.0f); };
		game.focusHeightEase = true;
		game.direction = 90.0f; // The camera looks to the edge of the map.
		game.Start({30.0f, 0.0f, 0.0f}, 0.5f);
		RunFor(harness, 0.5f, deltaTime);
		float largestStepExcess = 0.0f;
		float lowest = 1e9f;
		for (int frame = 0; frame < rate * 4; ++frame)
		{
			// Zoom out and in with the wheel, with the hand near the edge.
			game.wheel = frame < rate * 2 ? (frame % (rate / 6) == 0 ? -0.1f : 0.0f) : (frame % (rate / 6) == 0 ? 0.1f : 0.0f);
			const auto before = harness.Shown();
			harness.Frame(deltaTime);
			const auto shown = harness.Shown();
			const auto above = shown.y - game.GroundAt(shown.x, shown.z);
			lowest = std::min(lowest, above);
			// The rule of the camera tests: no move of more than 50 m or the height above the land in one frame.
			largestStepExcess = std::max(largestStepExcess, Length(shown - before) - std::max(50.0f, above));
		}
		game.wheel = 0.0f;
		EXPECT_LE(largestStepExcess, 0.0f);
		EXPECT_GE(lowest, -1e-3f);
		EXPECT_TRUE(game.errors.empty());
	}
}

TEST(SpringArm, TheCameraBesideASlopeStopsWithTheDefaultFeel)
{
	// Camera test camera-beside-slope in the game (2026-10-20): after a pitch and a turn next to a slope, the camera on
	// the arm and the eased push of the camera ball made a cycle of moves of 0.1 to 0.4 m every 0.2 s, and never stopped.
	for (const auto rate : k_Rates)
	{
		SCOPED_TRACE(std::to_string(rate) + " fps");
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		auto settings = RigSettings();
		settings.pitch = true;
		settings.lookUp = true;
		CameraLoopHarness harness(settings);
		auto& game = harness.game;
		game.focusHeightEase = true;
		game.turnDegreesPerPixel = 0.11f;
		game.mapMinimum = -1e6f;
		game.mapMaximum = 1e6f;
		game.mouse = {1100.0f, 300.0f, 0.0f};
		game.Start({}, 0.0f);
		RunFor(harness, 1.0f, deltaTime);
		const auto start = game.transform.position;
		const auto focus = game.focus;
		// A rough slope that rises 8 metres in 4 metres beside the camera, with small bumps.
		const auto back = Normalize(WorldPoint {start.x - focus.x, 0.0f, start.z - focus.z});
		const WorldPoint side {back.z, 0.0f, -back.x};
		game.ground = [side, back, start](float x, float z) {
			const auto across = (x - start.x) * side.x + (z - start.z) * side.z - 2.0f;
			const auto behind = (x - start.x) * back.x + (z - start.z) * back.z + 1.0f;
			return 100.0f + 8.0f * std::clamp(std::max(across, behind) / 4.0f, 0.0f, 1.0f) +
			       0.3f * std::sin(x * 1.7f) * std::cos(z * 1.3f);
		};
		game.buttons[2] = true;
		harness.Frame(deltaTime);
		for (int frame = 0; frame < rate; ++frame)
		{
			game.mouse.y += 120.0f * deltaTime;
			game.mouse.x += 300.0f * deltaTime;
			harness.Frame(deltaTime);
		}
		game.buttons[2] = false;
		RunFor(harness, 3.0f, deltaTime);
		float largestMove = 0.0f;
		auto previous = game.transform.position;
		for (int frame = 0; frame < rate; ++frame)
		{
			harness.Frame(deltaTime);
			largestMove = std::max(largestMove, Length(game.transform.position - previous));
			previous = game.transform.position;
		}
		EXPECT_LT(largestMove, 1e-3f);
		EXPECT_TRUE(game.errors.empty());
	}
}

TEST(SpringArm, TheCameraStopsNextToSlopesWithFrameTimesThatChange)
{
	// The cycle of camera-beside-slope in the game came with frame times that change from frame to frame. This sweep of
	// slopes, turns and pitches, with frame times of 0.8 to 1.2 of 1/60 s, checks that the camera stops in each case. It
	// did not reproduce the cycle of the game; the in-game scenario checks it.
	const auto deltaTime = 1.0f / 60.0f;
	int unstable = 0;
	for (float rise : {6.0f, 8.4f, 14.0f})
	{
		for (float offset : {-3.0f, -1.0f, 0.5f, 2.0f})
		{
			for (float turn : {100.0f, 300.0f, 600.0f})
			{
				for (float up : {-60.0f, 60.0f, 150.0f})
				{
					auto settings = RigSettings();
					settings.pitch = true;
					settings.lookUp = true;
					CameraLoopHarness harness(settings);
					auto& game = harness.game;
					game.focusHeightEase = true;
					game.turnDegreesPerPixel = 0.11f;
					game.mapMinimum = -1e6f;
					game.mapMaximum = 1e6f;
					game.mouse = {1100.0f, 300.0f, 0.0f};
					game.Start({}, 0.0f);
					RunFor(harness, 1.0f, deltaTime);
					const auto start = game.transform.position;
					const auto focus = game.focus;
					const auto back = Normalize(WorldPoint {start.x - focus.x, 0.0f, start.z - focus.z});
					game.ground = [=](float x, float z) {
						const auto along = (x - focus.x) * back.x + (z - focus.z) * back.z -
						                   Length(WorldPoint {start.x - focus.x, 0.0f, start.z - focus.z}) - offset;
						return 100.0f + rise * std::clamp(along / 4.0f, 0.0f, 1.0f) +
						       0.3f * std::sin(x * 1.7f) * std::cos(z * 1.3f);
					};
					game.buttons[2] = true;
					harness.Frame(deltaTime);
					for (int frame = 0; frame < 60; ++frame)
					{
						game.mouse.y += up / 60.0f;
						game.mouse.x += turn / 60.0f;
						harness.Frame(deltaTime);
					}
					game.buttons[2] = false;
					unsigned seed = 12345U;
					auto jitter = [&] {
						seed = seed * 1664525U + 1013904223U;
						return deltaTime * (0.8f + 0.4f * static_cast<float>(seed >> 8) / 16777216.0f);
					};
					for (int frame = 0; frame < 180; ++frame)
					{
						harness.Frame(jitter());
					}
					float largestMove = 0.0f;
					auto previous = game.transform.position;
					for (int frame = 0; frame < 60; ++frame)
					{
						harness.Frame(jitter());
						largestMove = std::max(largestMove, Length(game.transform.position - previous));
						previous = game.transform.position;
					}
					if (largestMove > 1e-3f)
					{
						++unstable;
					}
				}
			}
		}
	}
	EXPECT_EQ(unstable, 0);
}

TEST(KeyPanChase, TheGlideAfterTheGripGoesTheSameDistanceWithTheChase)
{
	const auto deltaTime = 1.0f / 60.0f;
	const auto travel = [&](double chaseSeconds) {
		auto settings = RigSettings();
		settings.chaseSeconds = chaseSeconds;
		auto harness = Settled(settings, 1.0f, deltaTime);
		auto& game = harness.game;
		game.mouse = {900.0f, 500.0f, 0.0f};
		harness.Frame(deltaTime);
		game.buttons[1] = true;
		harness.Frame(deltaTime);
		for (int frame = 0; frame < 30; ++frame)
		{
			game.mouse.x += 600.0f * deltaTime;
			harness.Frame(deltaTime);
		}
		game.buttons[1] = false;
		const auto released = harness.Shown();
		RunFor(harness, 3.0f, deltaTime);
		EXPECT_TRUE(game.errors.empty());
		return Length(harness.Shown() - released);
	};
	const auto plain = travel(0.0);
	const auto chased = travel(0.3);
	EXPECT_GT(plain, 1.0f);
	EXPECT_NEAR(chased, plain, 0.1f * plain);
}

TEST(MouseSmoothing, TurnsTheSameAtEachFrameRate)
{
	std::vector<std::vector<float>> headings;
	for (const auto rate : k_Rates)
	{
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		auto settings = RigSettings();
		settings.mouseSmoothing = 0.03;
		settings.turnInertia = 0.0;
		auto harness = Settled(settings, 1.0f, deltaTime);
		auto& game = harness.game;
		const auto start = game.direction;
		const auto framesPerSample = static_cast<int>(std::lround(k_CommonStep * static_cast<float>(rate)));
		std::vector<float> samples;
		for (int frame = 1; frame <= rate; ++frame)
		{
			game.buttons[2] = frame <= rate / 2;
			game.turnPerSecond = frame <= rate / 2 ? 90.0f : 0.0f;
			harness.Frame(deltaTime);
			if (frame % framesPerSample == 0)
			{
				samples.push_back(WrapDegrees(game.direction - start));
			}
		}
		// All of the turn comes out after the release.
		EXPECT_NEAR(samples.back(), 45.0f, 0.05f) << rate;
		headings.push_back(samples);
		EXPECT_TRUE(game.errors.empty());
	}
	for (size_t rate = 0; rate < headings.size(); ++rate)
	{
		for (size_t index = 0; index < headings[rate].size(); ++index)
		{
			EXPECT_NEAR(headings[rate][index], headings.back()[index], 0.05f) << k_Rates[rate] << " fps, " << index;
		}
	}
}

} // namespace
