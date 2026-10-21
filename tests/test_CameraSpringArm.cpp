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

namespace
{

/// The largest move of the pivot on the screen that the tests accept, pixels.
constexpr float k_PivotPixels = 0.5f;
/// The game turns by this angle for each pixel of a horizontal middle button drag.
constexpr float k_TurnDegreesPerPixel = 0.3f;
constexpr float k_PitchSensitivity = 30.0f;
constexpr std::array k_Rates {30, 60, 144};

/// The settings of the hand rig, with or without the spring arm.
[[nodiscard]] moa_mod::camera::CameraSettings ArmSettings(bool springArm)
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
	settings.springArm = springArm;
	// These tests are of the spring arm with the objects: the object collision is on.
	settings.objectCollision = true;
	return settings;
}

/// A harness on flat land at Detail, with the hand near the bottom of the screen, settled for one second.
[[nodiscard]] CameraLoopHarness Settled(float deltaTime, const moa_mod::camera::CameraSettings& settings)
{
	CameraLoopHarness harness(settings);
	auto& game = harness.game;
	game.focusHeightEase = true;
	game.turnDegreesPerPixel = k_TurnDegreesPerPixel;
	game.mouse = {1100.0f, 300.0f, 0.0f};
	game.Start({}, 0.0f);
	for (float time = 0.0f; time < 1.0f; time += deltaTime)
	{
		harness.Frame(deltaTime);
	}
	return harness;
}

[[nodiscard]] std::optional<Vector2> ScreenOf(const CameraLoopHarness& harness, WorldPoint point)
{
	return ScreenPoint(harness.game.ScreenCameraOf(harness.game.transform), point);
}

/// What the camera did about the pivot of a gesture.
struct ArmWatch
{
	WorldPoint pivot;
	Vector2 screen;
	float startDistance = 0.0f;
	float largestDistance = 0.0f;  ///< The largest distance from the pivot to the shown camera.
	float smallestDistance = 1e9f; ///< The smallest distance from the pivot to the shown camera.
	float nearestObstacle = 1e9f;  ///< The smallest distance from the shown camera to an obstacle.
	float largestDrift = 0.0f;     ///< The largest move of the pivot on the screen, in the frames that kept it.
	float lowest = 1e9f;           ///< The lowest height of the camera above the exact land.
	int frames = 0;
	int keptFrames = 0;
	int shortFrames = 0; ///< The frames in which the arm was shorter than the desired distance.

	void Start(const CameraLoopHarness& harness)
	{
		pivot = harness.loop.Trace().pivot.value_or(WorldPoint {});
		screen = ScreenOf(harness, pivot).value_or(Vector2 {});
		startDistance = Distance(harness);
	}

	[[nodiscard]] float Distance(const CameraLoopHarness& harness) const
	{
		return Length(harness.game.transform.position - pivot);
	}

	void Add(const CameraLoopHarness& harness)
	{
		const auto& game = harness.game;
		const auto& trace = harness.loop.Trace();
		++frames;
		const auto distance = Distance(harness);
		largestDistance = std::max(largestDistance, distance);
		smallestDistance = std::min(smallestDistance, distance);
		nearestObstacle = std::min(nearestObstacle, game.ObstacleClearance(game.transform.position));
		lowest =
		    std::min(lowest, game.transform.position.y - game.GroundAt(game.transform.position.x, game.transform.position.z));
		if (trace.armDistance && trace.desiredDistance && *trace.armDistance < *trace.desiredDistance - 1e-3f)
		{
			++shortFrames;
		}
		if (trace.pivotKept)
		{
			++keptFrames;
			if (auto at = ScreenOf(harness, pivot))
			{
				largestDrift = std::max(largestDrift, std::hypot(at->x - screen.x, at->y - screen.y));
			}
			else
			{
				largestDrift = 1e9f;
			}
		}
		else if (auto at = ScreenOf(harness, pivot))
		{
			screen = *at;
		}
	}
};

/// Hold the middle button and move the mouse by (x, y) pixels in each frame.
void HeldMoves(CameraLoopHarness& harness, float x, float y, int frames, float deltaTime, ArmWatch* watch)
{
	auto& game = harness.game;
	for (int frame = 0; frame < frames; ++frame)
	{
		game.mouse.x += x;
		game.mouse.y += y;
		harness.Frame(deltaTime);
		if (watch != nullptr)
		{
			watch->Add(harness);
		}
	}
}

void Idle(CameraLoopHarness& harness, float seconds, float deltaTime, ArmWatch* watch)
{
	for (float time = 0.0f; time < seconds; time += deltaTime)
	{
		harness.Frame(deltaTime);
		if (watch != nullptr)
		{
			watch->Add(harness);
		}
	}
}

/// The horizontal direction from the pivot to a point, turned by an angle about the vertical axis, at a distance.
[[nodiscard]] WorldPoint AroundPivot(WorldPoint pivot, WorldPoint from, float degrees, float radius, float height)
{
	const auto turned = TurnAboutPivot({from.x, pivot.y, from.z}, pivot, degrees) - pivot;
	const auto flat = Length(turned);
	return pivot + turned * (radius / flat) + WorldPoint {0.0f, height, 0.0f};
}

/// Put a row of buildings and trees on the circle of the camera about the pivot, away from the start of the camera.
void AddBuildingRow(FakeCameraGame& game, WorldPoint pivot, WorldPoint camera)
{
	const auto radius = std::hypot(camera.x - pivot.x, camera.z - pivot.z);
	const auto height = camera.y - pivot.y;
	// Buildings that cut the circle of the camera, and one that stands between the pivot and the circle.
	for (const auto& [degrees, at] :
	     {std::pair {75.0f, 1.0f}, std::pair {130.0f, 1.05f}, std::pair {200.0f, 0.85f}, std::pair {290.0f, 1.0f}})
	{
		const auto centre = AroundPivot(pivot, camera, degrees, radius * at, 0.0f);
		game.boxes.push_back({.minimum = {centre.x - 1.5f, pivot.y - 1.0f, centre.z - 1.5f},
		                      .maximum = {centre.x + 1.5f, pivot.y + height + 8.0f, centre.z + 1.5f}});
	}
	// Trees: a crown on the circle, and one inside it.
	game.obstacles.push_back({.centre = AroundPivot(pivot, camera, 165.0f, radius, height), .radius = 1.2f});
	game.obstacles.push_back({.centre = AroundPivot(pivot, camera, 250.0f, radius * 0.8f, height * 0.8f), .radius = 1.0f});
}

/// The result of a full turn about the hand near a row of buildings.
struct TurnResult
{
	ArmWatch watch;
	float endDistance = 0.0f;
	int gamePushes = 0;
	int casts = 0;
	std::vector<std::string> errors;
	std::vector<std::string> armEvents; ///< The events of the spring arm.
};

/// The options of a full turn near a row of buildings.
struct TurnOptions
{
	bool springArm = true;
	bool castAvailable = true; ///< The game has the sphere test of its camera.
	bool hills = false;        ///< A hill stands on the circle of the camera.
};

[[nodiscard]] TurnResult TurnNearBuildings(int rate, TurnOptions options)
{
	const auto deltaTime = 1.0f / static_cast<float>(rate);
	auto harness = Settled(deltaTime, ArmSettings(options.springArm));
	auto& game = harness.game;
	game.castAvailable = options.castAvailable;
	game.casts = 0;
	TurnResult result;
	// The press takes the pivot.
	game.buttons[2] = true;
	harness.Frame(deltaTime);
	result.watch.Start(harness);
	const auto pivot = result.watch.pivot;
	const auto camera = game.transform.position;
	AddBuildingRow(game, pivot, camera);
	if (options.hills)
	{
		// A round hill on the circle of the camera, a quarter turn from the start, higher than the camera.
		const auto top = AroundPivot(pivot, camera, 100.0f, std::hypot(camera.x - pivot.x, camera.z - pivot.z), 0.0f);
		const auto rise = camera.y - pivot.y + 4.0f;
		game.ground = [top, rise](float x, float z) {
			const auto distance = std::hypot(x - top.x, z - top.z);
			return 100.0f + rise * std::max(0.0f, 1.0f - distance / 6.0f);
		};
	}
	const auto pushes = game.pushes + game.collisions;
	// A full turn in 4 seconds at each frame rate.
	const auto frames = static_cast<int>(std::round(4.0f / deltaTime));
	const auto pixels = 360.0f / k_TurnDegreesPerPixel / static_cast<float>(frames);
	HeldMoves(harness, pixels, 0.0f, frames, deltaTime, &result.watch);
	game.buttons[2] = false;
	Idle(harness, 1.5f, deltaTime, &result.watch);
	result.endDistance = result.watch.Distance(harness);
	result.gamePushes = game.pushes + game.collisions - pushes;
	result.casts = game.casts;
	result.errors = game.errors;
	for (const auto& [kind, text] : game.events)
	{
		if (kind == CameraEventKind::Arm)
		{
			result.armEvents.push_back(text);
		}
	}
	return result;
}

void Report(const char* name, int rate, const TurnResult& result)
{
	const auto& watch = result.watch;
	std::cout << name << ", " << rate << " fps: start " << watch.startDistance << " m, largest " << watch.largestDistance
	          << " m, smallest " << watch.smallestDistance << " m, end " << result.endDistance << " m, nearest obstacle "
	          << watch.nearestObstacle << " m, arm shorter in " << watch.shortFrames << " of " << watch.frames
	          << " frames, pivot kept in " << watch.keptFrames << ", drift " << watch.largestDrift << " px, game pushes "
	          << result.gamePushes << "\n";
}

/// Check that the spring arm kept the camera in front of the obstacles and returned to the start distance.
void ExpectArm(const TurnResult& result, bool expectShort = true)
{
	const auto& watch = result.watch;
	EXPECT_TRUE(result.errors.empty());
	if (expectShort)
	{
		EXPECT_GT(watch.shortFrames, 0);
	}
	// The camera never went farther from the pivot than at the start, and never into an obstacle.
	EXPECT_LE(watch.largestDistance, watch.startDistance + 1e-3f);
	EXPECT_GE(watch.nearestObstacle, 0.0f);
	// After the obstacles, the camera is at the start distance again.
	EXPECT_NEAR(result.endDistance, watch.startDistance, 0.01f);
	// The pivot stays at its place on the screen, and the camera above the land.
	EXPECT_LE(watch.largestDrift, k_PivotPixels);
	EXPECT_GE(watch.lowest, 1.5f - 1e-3f);
	// The game did not push the camera.
	EXPECT_EQ(result.gamePushes, 0);
}

} // namespace

TEST(HandRigSpringArm, ATurnNearBuildingsComesBackToTheStartDistance)
{
	for (const auto rate : k_Rates)
	{
		SCOPED_TRACE(std::to_string(rate) + " fps");
		const auto result = TurnNearBuildings(rate, {});
		Report("spring arm", rate, result);
		ExpectArm(result);
		EXPECT_GT(result.casts, 0);
		// The events name the object that the arm touches, and tell when the arm is clear again.
		ASSERT_FALSE(result.armEvents.empty());
		EXPECT_TRUE(result.armEvents.front().starts_with("arm hit 'obstacle' at "));
		EXPECT_TRUE(
		    std::ranges::any_of(result.armEvents, [](const std::string& text) { return text == "arm clear of 'obstacle'"; }));
	}
}

TEST(HandRigSpringArm, WithNoSpringArmTheGameMovesTheCameraAwayFromTheHand)
{
	// The old rig took the push of the game as the truth: the camera went away from the hand and did not come back.
	for (const auto rate : k_Rates)
	{
		SCOPED_TRACE(std::to_string(rate) + " fps");
		const auto result = TurnNearBuildings(rate, {.springArm = false});
		Report("no spring arm", rate, result);
		EXPECT_GT(result.gamePushes, 0);
		EXPECT_GT(result.watch.largestDistance, result.watch.startDistance + 2.0f);
		EXPECT_GT(result.endDistance, result.watch.startDistance + 2.0f);
	}
}

TEST(HandRigSpringArm, AHillOnTheCircleComesBackToTheStartDistance)
{
	for (const auto rate : k_Rates)
	{
		SCOPED_TRACE(std::to_string(rate) + " fps");
		const auto result = TurnNearBuildings(rate, {.hills = true});
		Report("spring arm with a hill", rate, result);
		ExpectArm(result);
		const auto old = TurnNearBuildings(rate, {.springArm = false, .hills = true});
		Report("no spring arm with a hill", rate, old);
		EXPECT_GT(old.endDistance, old.watch.startDistance + 2.0f);
	}
}

TEST(HandRigSpringArm, WithNoSphereTestTheArmUsesOnlyTheLand)
{
	for (const auto rate : k_Rates)
	{
		SCOPED_TRACE(std::to_string(rate) + " fps");
		const auto result = TurnNearBuildings(rate, {.castAvailable = false, .hills = true});
		Report("land only", rate, result);
		EXPECT_TRUE(result.errors.empty());
		EXPECT_EQ(result.casts, 0);
		// The hill still pulls the camera in, and the camera comes back. The game does not push it away.
		EXPECT_GT(result.watch.shortFrames, 0);
		// A pushed camera can be farther than the desired distance by the dead band of the arm (5 cm), so that the arm and
		// the eased push of the camera ball do not move the camera in turn.
		EXPECT_LE(result.watch.largestDistance, result.watch.startDistance + 0.05f + 2e-3f);
		EXPECT_NEAR(result.endDistance, result.watch.startDistance, 0.01f);
		EXPECT_EQ(result.gamePushes, 0);
	}
}

TEST(HandRigSpringArm, APitchNearTheGroundStopsInFrontOfAnObstacleBehind)
{
	for (const auto springArm : {true, false})
	{
		for (const auto rate : k_Rates)
		{
			SCOPED_TRACE(std::to_string(rate) + " fps, spring arm " + std::to_string(springArm));
			const auto deltaTime = 1.0f / static_cast<float>(rate);
			auto harness = Settled(deltaTime, ArmSettings(springArm));
			auto& game = harness.game;
			ArmWatch watch;
			game.buttons[2] = true;
			harness.Frame(deltaTime);
			watch.Start(harness);
			// A low wall behind the camera. A flatter view brings the camera down and back into it.
			const auto pivot = watch.pivot;
			const auto camera = game.transform.position;
			const auto back = AroundPivot(pivot, camera, 0.0f, 1.0f, 0.0f) - pivot;
			const auto wallCentre =
			    AroundPivot(pivot, camera, 0.0f, std::hypot(camera.x - pivot.x, camera.z - pivot.z) + 1.6f, 0.0f);
			const WorldPoint across {-back.z, 0.0f, back.x};
			const auto corner = [&](float along, float side) { return wallCentre + back * along + across * side; };
			const auto a = corner(-0.5f, -6.0f);
			const auto b = corner(6.0f, 6.0f);
			game.boxes.push_back({.minimum = {std::min(a.x, b.x), pivot.y - 1.0f, std::min(a.z, b.z)},
			                      .maximum = {std::max(a.x, b.x), camera.y - 1.0f, std::max(a.z, b.z)}});
			ASSERT_GT(game.ObstacleClearance(camera), 0.6f);
			// The same moves of the mouse for each second at each frame rate: 15 degrees flatter, then back.
			const auto frames = static_cast<int>(std::round(0.5f / deltaTime));
			const auto pixels = 50.0f / static_cast<float>(frames);
			HeldMoves(harness, 0.0f, pixels, frames, deltaTime, &watch);
			Idle(harness, 0.5f, deltaTime, &watch);
			HeldMoves(harness, 0.0f, -pixels, frames, deltaTime, &watch);
			game.buttons[2] = false;
			Idle(harness, 1.0f, deltaTime, &watch);
			TurnResult result {.watch = watch, .endDistance = watch.Distance(harness), .errors = game.errors};
			Report(springArm ? "pitch, spring arm" : "pitch, no spring arm", rate, result);
			if (springArm)
			{
				ExpectArm(result);
			}
			else
			{
				// The old rig puts the camera into the wall.
				EXPECT_LT(watch.nearestObstacle, 0.0f);
			}
		}
	}
}

TEST(HandRigSpringArm, ASphereThatStartsInAnObjectDoesNotPullTheCameraToThePivot)
{
	for (const auto rate : k_Rates)
	{
		SCOPED_TRACE(std::to_string(rate) + " fps");
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		auto harness = Settled(deltaTime, ArmSettings(true));
		auto& game = harness.game;
		ArmWatch watch;
		game.buttons[2] = true;
		harness.Frame(deltaTime);
		watch.Start(harness);
		// A low object around the pivot, for example a field or a floor. The sphere of the arm starts in it.
		const auto pivot = watch.pivot;
		game.boxes.push_back({.minimum = {pivot.x - 1.0f, pivot.y - 1.0f, pivot.z - 1.0f},
		                      .maximum = {pivot.x + 1.0f, pivot.y + 0.3f, pivot.z + 1.0f}});
		HeldMoves(harness, 2.0f, 0.0f, static_cast<int>(1.0f / deltaTime), deltaTime, &watch);
		EXPECT_GT(game.casts, 0);
		EXPECT_EQ(watch.shortFrames, 0);
		EXPECT_NEAR(watch.Distance(harness), watch.startDistance, 0.01f);
		EXPECT_TRUE(game.errors.empty());
	}
}

TEST(HandRigSpringArm, AZoomScalesTheDistanceAndAKeyPanKeepsIt)
{
	const auto deltaTime = 1.0f / 60.0f;
	auto harness = Settled(deltaTime, ArmSettings(true));
	auto& game = harness.game;
	ArmWatch watch;
	game.buttons[2] = true;
	harness.Frame(deltaTime);
	watch.Start(harness);
	// A tree between the pivot and the camera pulls the camera in.
	const auto pivot = watch.pivot;
	const auto camera = game.transform.position;
	const auto radius = std::hypot(camera.x - pivot.x, camera.z - pivot.z);
	game.obstacles.push_back(
	    {.centre = AroundPivot(pivot, camera, 0.0f, radius * 0.75f, (camera.y - pivot.y) * 0.75f), .radius = 0.8f});
	game.buttons[2] = false;
	Idle(harness, 0.5f, deltaTime, &watch);
	const auto pulledIn = watch.Distance(harness);
	EXPECT_LT(pulledIn, watch.startDistance * 0.8f);
	// A wheel notch out: the zoom starts from the camera that the user sees, and sets the new distance of the arm.
	game.wheel = -0.1f;
	harness.Frame(deltaTime);
	game.wheel = 0.0f;
	Idle(harness, 1.5f, deltaTime, nullptr);
	const auto& trace = harness.loop.Trace();
	ASSERT_TRUE(trace.desiredDistance);
	ASSERT_TRUE(trace.armDistance);
	// The tree still holds the camera. The new distance comes from the zoom of the shown camera, not of the old rig.
	EXPECT_LE(*trace.armDistance, *trace.desiredDistance);
	EXPECT_LT(*trace.desiredDistance, watch.startDistance * 3.0f);
	EXPECT_GE(game.ObstacleClearance(game.transform.position), 0.0f);
	// A key pan moves the camera. The spring arm moves with it and keeps its desired distance: a camera that the tree
	// pulled in never gives the rig a shorter distance (run 9).
	const auto desiredBeforePan = *trace.desiredDistance;
	game.pan = {0.0f, 0.0f, 20.0f};
	harness.Frame(deltaTime);
	game.pan = {};
	EXPECT_FALSE(harness.loop.Trace().pivotKept);
	Idle(harness, 1.0f, deltaTime, nullptr);
	ASSERT_TRUE(harness.loop.Trace().desiredDistance);
	EXPECT_NEAR(*harness.loop.Trace().desiredDistance, desiredBeforePan, 1e-3f);
	EXPECT_GE(game.ObstacleClearance(game.transform.position), 0.0f);
	EXPECT_TRUE(game.errors.empty());
}

namespace
{

/// The smallest distance from the pivot to the rig that the loop keeps, metres.
constexpr float k_SmallestDesired = 1.0f;

/// The camera must stay this far above the exact land (the clearance), less a small tolerance.
[[nodiscard]] float HeightAboveTheRule(const FakeCameraGame& game)
{
	return game.transform.position.y - (game.GroundAt(game.transform.position.x, game.transform.position.z) + 1.5f);
}

} // namespace

TEST(HandRigSpringArm, TheEaseOfAZoomDoesNotTakeTheDistanceOfAPulledInCamera)
{
	// Run 9 (2026-10-19): a hill pulled the arm in, and the continuous zoom still eased by a very small step in each
	// frame. Each step of the zoom started from the camera that the hill pulled in, so the desired distance became the
	// pulled-in distance, then zero. The rig then stayed on the pivot, under the land.
	for (const auto rate : k_Rates)
	{
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		auto settings = ArmSettings(true);
		settings.continuousZoom = true;
		auto harness = Settled(deltaTime, settings);
		auto& game = harness.game;
		ArmWatch watch;
		game.buttons[2] = true;
		harness.Frame(deltaTime);
		watch.Start(harness);
		const auto pivot = watch.pivot;
		const auto camera = game.transform.position;
		const auto radius = std::hypot(camera.x - pivot.x, camera.z - pivot.z);
		game.obstacles.push_back(
		    {.centre = AroundPivot(pivot, camera, 0.0f, radius * 0.6f, (camera.y - pivot.y) * 0.6f), .radius = 0.8f});
		// One wheel notch out while the tree pulls the arm in. The zoom eases for some frames.
		game.wheel = -0.1f;
		harness.Frame(deltaTime);
		game.wheel = 0.0f;
		float smallestDesired = 1e9f;
		for (float time = 0.0f; time < 1.5f; time += deltaTime)
		{
			harness.Frame(deltaTime);
			watch.Add(harness);
			if (const auto desired = harness.loop.Trace().desiredDistance)
			{
				smallestDesired = std::min(smallestDesired, *desired);
			}
		}
		game.buttons[2] = false;
		Idle(harness, 0.5f, deltaTime, &watch);
		const auto& trace = harness.loop.Trace();
		ASSERT_TRUE(trace.desiredDistance);
		std::cout << rate << " fps: start " << watch.startDistance << " m, smallest desired " << smallestDesired
		          << " m, desired at the end " << *trace.desiredDistance << " m, nearest camera " << watch.smallestDistance
		          << " m" << std::endl;
		// One notch makes the height 1.25 times larger. The tree does not change the desired distance.
		EXPECT_NEAR(*trace.desiredDistance, watch.startDistance * 1.25f, watch.startDistance * 0.1f) << rate;
		EXPECT_GT(smallestDesired, watch.startDistance * 0.95f) << rate;
		EXPECT_LT(watch.smallestDistance, watch.startDistance * 0.7f) << rate;
		EXPECT_GE(watch.nearestObstacle, 0.0f) << rate;
		EXPECT_TRUE(game.errors.empty());
	}
}

TEST(HandRigSpringArm, PitchAndTurnNextToACliffKeepTheCameraAboveTheLand)
{
	// A cliff rises behind the camera. A flat pitch and a turn make the arm meet the cliff, while the zoom eases. The
	// arm never gets shorter than its smallest length, the camera rises over the land instead, and the rig never
	// loses its distance to the pivot.
	for (const auto rate : k_Rates)
	{
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		auto settings = ArmSettings(true);
		settings.continuousZoom = true;
		auto harness = Settled(deltaTime, settings);
		auto& game = harness.game;
		const auto start = game.transform.position;
		const auto focus = game.focus;
		// The land rises 40 metres in 4 metres, 3 metres behind the camera, across the full width of the view.
		const auto back = Normalize(WorldPoint {start.x - focus.x, 0.0f, start.z - focus.z});
		const auto cliff = Dot(WorldPoint {start.x, 0.0f, start.z}, back) + 3.0f;
		game.ground = [back, cliff](float x, float z) {
			const auto along = x * back.x + z * back.z - cliff;
			return 100.0f + 40.0f * std::clamp(along / 4.0f, 0.0f, 1.0f);
		};
		ArmWatch watch;
		game.buttons[2] = true;
		harness.Frame(deltaTime);
		watch.Start(harness);
		float lowest = 1e9f;
		float smallestDesired = 1e9f;
		auto check = [&] {
			lowest = std::min(lowest, HeightAboveTheRule(game));
			if (const auto desired = harness.loop.Trace().desiredDistance)
			{
				smallestDesired = std::min(smallestDesired, *desired);
			}
		};
		// Flatter, then a turn with notches of the wheel, then steeper.
		for (int frame = 0; frame < 30; ++frame)
		{
			game.mouse.y += 4.0f;
			harness.Frame(deltaTime);
			check();
		}
		for (int frame = 0; frame < 120; ++frame)
		{
			game.mouse.x += 3.0f;
			game.wheel = frame % 30 == 0 ? (frame < 60 ? 0.1f : -0.1f) : 0.0f;
			harness.Frame(deltaTime);
			check();
		}
		game.wheel = 0.0f;
		for (int frame = 0; frame < 30; ++frame)
		{
			game.mouse.y -= 4.0f;
			harness.Frame(deltaTime);
			check();
		}
		game.buttons[2] = false;
		for (float time = 0.0f; time < 1.0f; time += deltaTime)
		{
			harness.Frame(deltaTime);
			check();
		}
		// A zoom out still works.
		const auto before = game.transform.position.y;
		game.wheel = -0.1f;
		harness.Frame(deltaTime);
		game.wheel = 0.0f;
		Idle(harness, 1.0f, deltaTime, nullptr);
		std::cout << rate << " fps: lowest above the land rule " << lowest << " m, smallest desired " << smallestDesired
		          << " m, zoom out " << game.transform.position.y - before << " m" << std::endl;
		EXPECT_GE(lowest, -0.01f) << rate;
		EXPECT_GE(smallestDesired, k_SmallestDesired) << rate;
		EXPECT_GT(game.transform.position.y, before + 0.5f) << rate;
		EXPECT_TRUE(game.errors.empty());
	}
}

TEST(HandRigSpringArm, AHandOnARoofDoesNotPullTheCameraOntoThePivot)
{
	// The hand is on the roof of a building, so the land under it (the pivot) is in the building. The sphere of the
	// arm starts in the building. The camera stays at its distance, above the land, and the rig keeps its distance.
	for (const auto rate : k_Rates)
	{
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		auto settings = ArmSettings(true);
		settings.continuousZoom = true;
		auto harness = Settled(deltaTime, settings);
		auto& game = harness.game;
		const auto land = game.LandAt(game.transform, game.mouse);
		ASSERT_TRUE(land);
		game.boxes.push_back({.minimum = {land->x - 4.0f, land->y - 1.0f, land->z - 4.0f},
		                      .maximum = {land->x + 4.0f, land->y + 6.0f, land->z + 4.0f}});
		ArmWatch watch;
		game.buttons[2] = true;
		harness.Frame(deltaTime);
		watch.Start(harness);
		float smallestDesired = 1e9f;
		for (int frame = 0; frame < 90; ++frame)
		{
			game.mouse.x += 4.0f;
			game.mouse.y += frame < 45 ? 2.0f : -2.0f;
			game.wheel = frame == 20 ? 0.1f : 0.0f;
			harness.Frame(deltaTime);
			watch.Add(harness);
			if (const auto desired = harness.loop.Trace().desiredDistance)
			{
				smallestDesired = std::min(smallestDesired, *desired);
			}
		}
		game.buttons[2] = false;
		Idle(harness, 0.5f, deltaTime, &watch);
		std::cout << rate << " fps: start " << watch.startDistance << " m, nearest camera " << watch.smallestDistance
		          << " m, smallest desired " << smallestDesired << " m, lowest " << watch.lowest << " m" << std::endl;
		EXPECT_GT(watch.smallestDistance, 2.0f) << rate;
		EXPECT_GT(smallestDesired, watch.startDistance * 0.5f) << rate;
		EXPECT_GE(watch.lowest, 1.5f - 0.01f) << rate;
		EXPECT_TRUE(game.errors.empty());
	}
}

TEST(HandRigReplay, Run9PitchAndTurnDoNotPutTheCameraUnderTheMap)
{
	// Middle drags 3 and 4 of run 9 (2026-10-19, main b1a8ea6): "The pitch+rotate put me under the map". In drag 3 a
	// hill pulled the arm in to 3 metres, the ease of the zoom gave the rig that distance, then zero, and the camera
	// stayed on the pivot under the land for all the later drags. The replay plays the mouse, the buttons, the wheel
	// and the turns of the run on land that the trace samples (the land under the cursor and under the camera).
	const auto frames =
	    moa_mod::camera::test::ReadTraceFrames(std::string(MOA_TEST_DATA_DIR) + "/camera-trace-run9-under-map.csv");
	ASSERT_GT(frames.size(), 300U);
	// The land: the nearest samples of the trace, weighted by the inverse square of the distance.
	struct Sample
	{
		float x = 0.0f;
		float z = 0.0f;
		float height = 0.0f;
	};
	std::vector<Sample> samples;
	for (const auto& frame : frames)
	{
		if (frame.cursorOnLand && Length(frame.cursorLand) > 0.0f)
		{
			samples.push_back({frame.cursorLand.x, frame.cursorLand.z, frame.cursorLand.y});
		}
		if (frame.highestGround > 0.0f)
		{
			samples.push_back({frame.oldCamera.x, frame.oldCamera.z, frame.highestGround - 2.0f});
		}
		if (Length(frame.hand) > 0.0f)
		{
			samples.push_back({frame.hand.x, frame.hand.z, frame.hand.y});
		}
	}
	ASSERT_GT(samples.size(), 100U);
	auto settings = ArmSettings(true);
	settings.turnInertia = 0.25;
	settings.pitchInertia = 0.25;
	settings.continuousZoom = true;
	settings.panSpeed = 0.45;
	settings.pitchSensitivity = 10.0;
	CameraLoopHarness harness(settings);
	auto& game = harness.game;
	game.screenWidth = 3840.0f;
	game.screenHeight = 1440.0f;
	game.focusHeightEase = true;
	game.ground = [samples](float x, float z) {
		float weights = 0.0f;
		float sum = 0.0f;
		for (const auto& sample : samples)
		{
			const auto distance = (sample.x - x) * (sample.x - x) + (sample.z - z) * (sample.z - z);
			if (distance < 1e-4f)
			{
				return sample.height;
			}
			const auto weight = 1.0f / (distance * distance);
			weights += weight;
			sum += weight * sample.height;
		}
		return sum / weights;
	};
	// At frame 12273 the arm of the run met the land 3.1 metres from the pivot, on the line to the camera, where the
	// land rose to approximately 110 metres. The samples of the trace are too far apart to show that bank, so the
	// replay puts a box there.
	const auto runPivot = WorldPoint {-61.0341f, 103.7313f, 66.7769f};
	const auto runPulledIn = WorldPoint {-59.1238f, 107.795f, 69.1131f};
	const auto bank = runPivot + Normalize(WorldPoint {runPulledIn.x - runPivot.x, 0.0f, runPulledIn.z - runPivot.z}) * 6.0f;
	game.boxes.push_back(
	    {.minimum = {bank.x - 3.0f, 100.0f, bank.z - 3.0f}, .maximum = {bank.x + 3.0f, 110.0f, bank.z + 3.0f}});
	const auto& first = frames.front();
	game.direction = first.direction;
	game.mouse = first.mouse;
	// In the run, a wheel notch in just before the extract zoomed from stage 1.15 to the stage of the drag (1.019), and
	// the ease of that zoom continued for the whole drag, by a very small step in each frame. The replay makes the
	// same notch.
	std::vector<StageView> table;
	for (const auto& row : FakeCameraGame::k_StageTable)
	{
		table.push_back({.height = row.height, .tilt = row.tilt});
	}
	const auto dragStage = frames[30].stage;
	game.Start(first.focus, StageOfHeight(table, HeightOfStage(table, dragStage) * 1.25f));
	constexpr float k_DeltaTime = 1.0f / 60.0f;
	for (int frame = 0; frame < 30; ++frame)
	{
		harness.Frame(k_DeltaTime);
	}
	game.wheel = 0.1f;
	harness.Frame(k_DeltaTime);
	game.wheel = 0.0f;
	float lowest = 1e9f;
	float smallestDesired = 1e9f;
	float smallestHeightPart = 1e9f;
	// In drag 3 the user only turns and pitches, with no zoom of the wheel. The desired distance stays near the
	// distance of the camera of the game at the stage of the drag.
	constexpr int k_LastFrameOfDrag3 = 12340;
	float smallestDrag3Desired = 1e9f;
	const auto dragTilt = InterpolateStage(table, dragStage).tilt * k_DegreesToRadians;
	const auto dragDistance = InterpolateStage(table, dragStage).height / std::sin(dragTilt);
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
		lowest = std::min(lowest, HeightAboveTheRule(game));
		if (const auto desired = harness.loop.Trace().desiredDistance)
		{
			smallestDesired = std::min(smallestDesired, *desired);
			if (frame.frame <= k_LastFrameOfDrag3)
			{
				smallestDrag3Desired = std::min(smallestDrag3Desired, *desired);
			}
		}
		smallestHeightPart = std::min(smallestHeightPart, game.height / InterpolateStage(table, game.stage).height);
	}
	std::cout << "run 9 replay: lowest above the land rule " << lowest << " m, smallest desired " << smallestDesired
	          << " m (in drag 3 " << smallestDrag3Desired << " m, stage distance " << dragDistance
	          << " m), smallest height of the game / stage height " << smallestHeightPart << std::endl;
	EXPECT_GE(lowest, -0.01f);
	EXPECT_GT(smallestDrag3Desired, dragDistance * 0.8f);
	EXPECT_GE(smallestDesired, k_SmallestDesired);
	// The floor of the camera is the exact land plus the clearance (before: the highest land near the camera plus 3.5
	// metres), so the camera can come nearer to the level of the pivot: about 1.9 metres above it at this stage.
	EXPECT_GT(smallestHeightPart, 0.05f);
	EXPECT_TRUE(game.errors.empty());
}

TEST(HandRigReplay, Run10PitchUpAtTheMapEdgeKeepsTheDesiredDistance)
{
	// Middle drag 2 of run 10 (2026-10-19, main 6100b13): at Detail the user pitched up past the land limit near
	// the edge of the map (z = -285). The view up put the focus of the game far away on the line of the heading, out of
	// the map, and the clamp of the game moved the camera and the arm by up to 37 metres in each frame. Each move gave
	// the spring arm a new pivot with the part that the bank took, so the desired distance grew from 24.5 to 229 metres
	// in 7 frames, and the camera flew far out each time the arm was clear.
	const auto frames =
	    moa_mod::camera::test::ReadTraceFrames(std::string(MOA_TEST_DATA_DIR) + "/camera-trace-run10-collision.csv");
	ASSERT_GT(frames.size(), 300U);
	struct Sample
	{
		float x = 0.0f;
		float z = 0.0f;
		float height = 0.0f;
	};
	std::vector<Sample> samples;
	for (const auto& frame : frames)
	{
		if (frame.cursorOnLand && Length(frame.cursorLand) > 0.0f)
		{
			samples.push_back({frame.cursorLand.x, frame.cursorLand.z, frame.cursorLand.y});
		}
		if (frame.highestGround > 0.0f)
		{
			samples.push_back({frame.oldCamera.x, frame.oldCamera.z, frame.highestGround - 2.0f});
		}
	}
	ASSERT_GT(samples.size(), 100U);
	auto settings = ArmSettings(true);
	settings.turnInertia = 0.25;
	settings.pitchInertia = 0.25;
	settings.continuousZoom = true;
	settings.panSpeed = 0.45;
	settings.pitchSensitivity = 10.0;
	CameraLoopHarness harness(settings);
	auto& game = harness.game;
	game.screenWidth = 3840.0f;
	game.screenHeight = 1440.0f;
	game.focusHeightEase = true;
	game.mapMinimum = -3000.0f;
	game.mapMinimumZ = -285.0f;
	game.ground = [samples](float x, float z) {
		float weights = 0.0f;
		float sum = 0.0f;
		for (const auto& sample : samples)
		{
			const auto distance = (sample.x - x) * (sample.x - x) + (sample.z - z) * (sample.z - z);
			if (distance < 1e-4f)
			{
				return sample.height;
			}
			const auto weight = 1.0f / (distance * distance);
			weights += weight;
			sum += weight * sample.height;
		}
		return sum / weights;
	};
	const auto& first = frames.front();
	game.direction = first.direction;
	game.mouse = first.mouse;
	game.Start(first.focus, first.stage);
	constexpr float k_DeltaTime = 1.0f / 60.0f;
	for (int frame = 0; frame < 30; ++frame)
	{
		harness.Frame(k_DeltaTime);
	}
	// The raise of the first frame: a vertical middle drag before the replay.
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
	float smallestDesired = 1e9f;
	float largestDesired = 0.0f;
	float largestBeyondDesired = 0.0f;
	float nearestObstacleFree = 1e9f;
	int corrections = 0;
	bool zoomed = false;
	for (const auto& frame : frames)
	{
		game.buttons[2] = frame.middle;
		game.buttons[1] = frame.right;
		game.mouse = game.mouse + frame.mouseDelta;
		game.mouse.x = std::clamp(game.mouse.x, 0.0f, game.screenWidth);
		game.mouse.y = std::clamp(game.mouse.y, 0.0f, game.screenHeight);
		game.wheel = frame.wheel;
		zoomed = zoomed || frame.wheel != 0.0f;
		game.turnPerSecond = WrapDegrees(frame.gameDirection - frame.direction) / frame.deltaTime;
		harness.Frame(frame.deltaTime);
		const auto& trace = harness.loop.Trace();
		corrections += trace.desiredChange == 4 ? 1 : 0;
		nearestObstacleFree = std::min(nearestObstacleFree, HeightAboveTheRule(game));
		if (zoomed || !trace.desiredDistance)
		{
			continue;
		}
		smallestDesired = std::min(smallestDesired, *trace.desiredDistance);
		largestDesired = std::max(largestDesired, *trace.desiredDistance);
		if (trace.pivot)
		{
			largestBeyondDesired =
			    std::max(largestBeyondDesired, Length(game.transform.position - *trace.pivot) - *trace.desiredDistance);
		}
	}
	std::cout << "run 10 replay: desired distance " << smallestDesired << " to " << largestDesired
	          << " m before the zoom, largest distance beyond the desired distance " << largestBeyondDesired
	          << " m, lowest above the land rule " << nearestObstacleFree << " m, corrections " << corrections << std::endl;
	// A turn and a pitch do not change the desired distance. A new pivot can change it a little.
	EXPECT_LT(largestDesired, smallestDesired * 1.5f);
	EXPECT_LT(largestDesired, 60.0f);
	EXPECT_LT(largestBeyondDesired, 1.0f);
	EXPECT_GE(nearestObstacleFree, -0.01f);
	EXPECT_TRUE(game.errors.empty());
}

TEST(HandRigSpringArm, APitchUpAtTheMapEdgeAndATurnPastBuildingsKeepTheDesiredDistance)
{
	// The view turns up past the land limit next to a bank, with the edge of the map in front of the camera, and then
	// the camera turns about the hand past a row of buildings. Only an obstacle makes the arm shorter: the desired
	// distance stays, the camera is never farther from the pivot than it, never in an obstacle, and the arm comes back
	// to the same distance when the line is clear.
	for (const auto rate : k_Rates)
	{
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		auto settings = ArmSettings(true);
		settings.continuousZoom = true;
		auto harness = Settled(deltaTime, settings);
		auto& game = harness.game;
		const auto camera = game.transform.position;
		const auto focus = game.focus;
		const auto ahead = Normalize(WorldPoint {focus.x - camera.x, 0.0f, focus.z - camera.z});
		// The edge of the map, 150 metres in front of the camera, across the heading (the heading of the harness is
		// along z or x; the box clamps both).
		if (std::abs(ahead.z) >= std::abs(ahead.x))
		{
			(ahead.z > 0.0f ? game.mapMaximum : game.mapMinimum) = camera.z + 150.0f * (ahead.z > 0.0f ? 1.0f : -1.0f);
		}
		else
		{
			(ahead.x > 0.0f ? game.mapMaximum : game.mapMinimum) = camera.x + 150.0f * (ahead.x > 0.0f ? 1.0f : -1.0f);
		}
		ArmWatch watch;
		game.buttons[2] = true;
		harness.Frame(deltaTime);
		watch.Start(harness);
		AddBuildingRow(game, watch.pivot, camera);
		// A bank behind the camera: the land rises 8 metres, 2 metres behind it.
		const auto behind = Dot(WorldPoint {camera.x, 0.0f, camera.z}, ahead * -1.0f) + 2.0f;
		game.ground = [ahead, behind](float x, float z) {
			const auto along = -(x * ahead.x + z * ahead.z) - behind;
			return 100.0f + 8.0f * std::clamp(along / 3.0f, 0.0f, 1.0f);
		};
		ASSERT_TRUE(harness.loop.Trace().desiredDistance);
		harness.Frame(deltaTime);
		const auto desired = *harness.loop.Trace().desiredDistance;
		float largestDesiredChange = 0.0f;
		float largestBeyond = 0.0f;
		int corrections = 0;
		auto check = [&] {
			const auto& trace = harness.loop.Trace();
			if (trace.desiredDistance)
			{
				largestDesiredChange = std::max(largestDesiredChange, std::abs(*trace.desiredDistance - desired));
			}
			largestBeyond = std::max(largestBeyond, watch.Distance(harness) - desired);
			corrections += trace.desiredChange == 4 ? 1 : 0;
		};
		// Up past the land limit and into the look up.
		for (int frame = 0; frame < 60; ++frame)
		{
			game.mouse.y += 6.0f;
			harness.Frame(deltaTime);
			watch.Add(harness);
			check();
		}
		// A full turn about the hand, then back down.
		for (int frame = 0; frame < 240; ++frame)
		{
			game.mouse.x += 5.0f;
			game.mouse.y -= frame < 60 ? 6.0f : 0.0f;
			harness.Frame(deltaTime);
			watch.Add(harness);
			check();
		}
		game.buttons[2] = false;
		Idle(harness, 1.5f, deltaTime, &watch);
		check();
		std::cout << rate << " fps: desired " << desired << " m, largest change " << largestDesiredChange
		          << " m, largest beyond " << largestBeyond << " m, nearest obstacle " << watch.nearestObstacle << " m, end "
		          << watch.Distance(harness) << " m, corrections " << corrections << std::endl;
		EXPECT_LT(largestDesiredChange, desired * 1e-3f) << rate;
		EXPECT_LT(largestBeyond, 0.01f) << rate;
		EXPECT_GE(watch.nearestObstacle, 0.0f) << rate;
		EXPECT_EQ(corrections, 0) << rate;
		EXPECT_TRUE(game.errors.empty());
	}
}

namespace
{

/// What the camera did in a test of the land at the pivot.
struct LandAtPivotResult
{
	float largestMove = 0.0f; ///< The largest move of the camera in one frame, metres.
	float lowest = 1e9f;      ///< The lowest height of the camera above the land rule.
	int shortFrames = 0;      ///< The frames in which the arm was shorter than the desired distance.
	int landHits = 0;         ///< The events of the arm that name the land collider.
	std::vector<std::string> errors;
};

/// Add the frame to the result. `previous` is the camera of the last frame.
void AddLandFrame(const CameraLoopHarness& harness, WorldPoint& previous, LandAtPivotResult& result)
{
	const auto& game = harness.game;
	const auto& trace = harness.loop.Trace();
	result.largestMove = std::max(result.largestMove, Length(game.transform.position - previous));
	previous = game.transform.position;
	result.lowest = std::min(result.lowest, HeightAboveTheRule(game));
	if (trace.armDistance && trace.desiredDistance && *trace.armDistance < *trace.desiredDistance - 1e-3f)
	{
		++result.shortFrames;
	}
}

void CountLandHits(const FakeCameraGame& game, LandAtPivotResult& result)
{
	result.landHits = static_cast<int>(std::ranges::count_if(game.events, [](const auto& event) {
		return event.first == CameraEventKind::Arm && event.second.starts_with("arm hit 'MoATerrain'");
	}));
	result.errors = game.errors;
}

/// The options of a turn on the land collider.
struct LandAtPivotOptions
{
	std::optional<float> obstacleAlong; ///< A round obstacle on the arm, at this distance from the pivot.
	bool slope = false;                 ///< The land rises steeply from the pivot to the camera, then less steeply.
};

/// A turn about the hand at zoom stage 1 (the camera 71 metres from the pivot), on land that is a collider.
[[nodiscard]] LandAtPivotResult TurnOnLandCollider(int rate, std::optional<int32_t> landLayer, LandAtPivotOptions options = {})
{
	const auto deltaTime = 1.0f / static_cast<float>(rate);
	CameraLoopHarness harness(ArmSettings(true));
	auto& game = harness.game;
	game.focusHeightEase = true;
	game.turnDegreesPerPixel = k_TurnDegreesPerPixel;
	game.mouse = {1100.0f, 300.0f, 0.0f};
	game.Start({}, 1.0f);
	Idle(harness, 1.0f, deltaTime, nullptr);
	game.landColliderLayer = landLayer;
	LandAtPivotResult result;
	auto previous = game.transform.position;
	game.buttons[2] = true;
	harness.Frame(deltaTime);
	AddLandFrame(harness, previous, result);
	const auto pivot = harness.loop.Trace().pivot.value_or(WorldPoint {});
	if (options.obstacleAlong)
	{
		const auto toCamera = Normalize(game.transform.position - pivot);
		game.obstacles.push_back({.centre = pivot + toCamera * *options.obstacleAlong, .radius = 1.0f});
	}
	if (options.slope)
	{
		// The land under the arm: flat for 2.3 metres from the pivot, then a slope of 60 degrees for 1.5 metres, then
		// a slope of 15 degrees. The arm (39 degrees) is above the land, but below the highest land within 1.5 metres
		// on the steep part.
		const auto back =
		    Normalize(WorldPoint {game.transform.position.x - pivot.x, 0.0f, game.transform.position.z - pivot.z});
		game.ground = [pivot, back](float x, float z) {
			const auto along = (x - pivot.x) * back.x + (z - pivot.z) * back.z;
			const auto steep = std::clamp(along - 2.3f, 0.0f, 1.5f) * std::tan(60.0f * k_DegreesToRadians);
			const auto gentle = std::max(along - 3.8f, 0.0f) * std::tan(15.0f * k_DegreesToRadians);
			return pivot.y + steep + gentle;
		};
	}
	const auto frames = static_cast<int>(std::round(1.0f / deltaTime));
	for (int frame = 0; frame < frames; ++frame)
	{
		game.mouse.x += 60.0f / static_cast<float>(frames);
		harness.Frame(deltaTime);
		AddLandFrame(harness, previous, result);
	}
	game.buttons[2] = false;
	for (float time = 0.0f; time < 1.0f; time += deltaTime)
	{
		harness.Frame(deltaTime);
		AddLandFrame(harness, previous, result);
	}
	CountLandHits(game, result);
	return result;
}

/// Replay an extract of a black box of the camera tests in the game (5120 x 1440 pixels), on land that the trace
/// samples. The land is a collider on `landLayer`.
[[nodiscard]] LandAtPivotResult ReplayOnLandCollider(const std::string& fixture, std::optional<int32_t> landLayer)
{
	const auto frames = moa_mod::camera::test::ReadTraceFrames(std::string(MOA_TEST_DATA_DIR) + "/" + fixture);
	EXPECT_GT(frames.size(), 60U);
	if (frames.empty())
	{
		return {};
	}
	struct Sample
	{
		float x = 0.0f;
		float z = 0.0f;
		float height = 0.0f;
	};
	std::vector<Sample> samples;
	for (const auto& frame : frames)
	{
		if (frame.cursorOnLand && Length(frame.cursorLand) > 0.0f)
		{
			samples.push_back({frame.cursorLand.x, frame.cursorLand.z, frame.cursorLand.y});
		}
		if (frame.highestGround > 0.0f)
		{
			samples.push_back({frame.oldCamera.x, frame.oldCamera.z, frame.highestGround - 2.0f});
		}
		if (Length(frame.hand) > 0.0f)
		{
			samples.push_back({frame.hand.x, frame.hand.z, frame.hand.y});
		}
	}
	auto settings = ArmSettings(true);
	settings.turnInertia = 0.25;
	settings.pitchInertia = 0.25;
	settings.continuousZoom = true;
	settings.panSpeed = 0.45;
	settings.pitchSensitivity = 10.0;
	CameraLoopHarness harness(settings);
	auto& game = harness.game;
	game.screenWidth = 5120.0f;
	game.screenHeight = 1440.0f;
	game.focusHeightEase = true;
	game.ground = [samples](float x, float z) {
		float weights = 0.0f;
		float sum = 0.0f;
		for (const auto& sample : samples)
		{
			const auto distance = (sample.x - x) * (sample.x - x) + (sample.z - z) * (sample.z - z);
			if (distance < 1e-4f)
			{
				return sample.height;
			}
			const auto weight = 1.0f / (distance * distance);
			weights += weight;
			sum += weight * sample.height;
		}
		return sum / weights;
	};
	const auto& first = frames.front();
	game.direction = first.direction;
	game.mouse = first.mouse;
	game.Start(first.focus, first.stage);
	constexpr float k_DeltaTime = 1.0f / 60.0f;
	Idle(harness, 0.5f, k_DeltaTime, nullptr);
	game.landColliderLayer = landLayer;
	LandAtPivotResult result;
	auto previous = game.transform.position;
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
		AddLandFrame(harness, previous, result);
	}
	CountLandHits(game, result);
	return result;
}

void Report(const char* name, const LandAtPivotResult& result)
{
	std::cout << name << ": largest move in one frame " << result.largestMove << " m, lowest above the land rule "
	          << result.lowest << " m, arm shorter in " << result.shortFrames << " frames, hits on the land collider "
	          << result.landHits << std::endl;
}

} // namespace

TEST(HandRigSpringArm, TheLandColliderAtThePivotDoesNotPullTheArmIn)
{
	// Camera tests 3 and 4 in the game (2026-10-19): the sphere of the arm touched the land collider 'MoATerrain' 0.3
	// to 1.4 metres from the pivot, which is on the land. The arm went from 64 metres to its shortest length (7.1
	// metres) in one frame. The land collider is on the land layer, and the arm does not test that layer.
	for (const auto rate : k_Rates)
	{
		SCOPED_TRACE(std::to_string(rate) + " fps");
		const auto result = TurnOnLandCollider(rate, k_LandLayer);
		Report("land collider on the land layer", result);
		EXPECT_EQ(result.landHits, 0);
		EXPECT_EQ(result.shortFrames, 0);
		EXPECT_LT(result.largestMove, 5.0f);
		EXPECT_GE(result.lowest, -0.01f);
		EXPECT_TRUE(result.errors.empty());
	}
}

TEST(HandRigSpringArm, ALandColliderInTheLayersDoesNotPullTheArmIn)
{
	// If the land collider were on a layer of the test, the sphere touches it near the pivot. The arm starts the
	// sphere again after a hit near the pivot, so the camera stays where it is.
	for (const auto rate : k_Rates)
	{
		SCOPED_TRACE(std::to_string(rate) + " fps");
		const auto result = TurnOnLandCollider(rate, 0);
		Report("land collider on the default layer", result);
		EXPECT_EQ(result.shortFrames, 0);
		EXPECT_LT(result.largestMove, 5.0f);
		EXPECT_GE(result.lowest, -0.01f);
		EXPECT_TRUE(result.errors.empty());
	}
}

TEST(HandRigSpringArm, AnObstacleNearThePivotPullsTheArmInWithTheEase)
{
	// A tree 4.5 metres from the pivot, on the arm: nearer to the pivot than the shortest arm (7.1 metres). The arm
	// cannot stop the camera in front of it, so the arm moves in with the slow ease, not in one frame.
	for (const auto rate : k_Rates)
	{
		SCOPED_TRACE(std::to_string(rate) + " fps");
		const auto result = TurnOnLandCollider(rate, k_LandLayer, {.obstacleAlong = 4.5f});
		Report("tree near the pivot", result);
		EXPECT_GT(result.shortFrames, 0);
		// The arm was 64 metres long. At 30 frames each second, the slow ease moves the camera by 11.5 metres.
		EXPECT_LT(result.largestMove, 15.0f);
		EXPECT_GE(result.lowest, -0.01f);
		EXPECT_TRUE(result.errors.empty());
	}
}

TEST(HandRigSpringArm, TheLandTestDoesNotStopTheArmOnTheSlopeAtThePivot)
{
	// Camera test 4, reset-view and pitch-up-map-edge: the land test of the arm stopped the arm 1.9 to 4 metres from
	// the pivot. The highest land near a point on the arm was above the arm there, but the arm was above the land.
	for (const auto rate : k_Rates)
	{
		SCOPED_TRACE(std::to_string(rate) + " fps");
		const auto result = TurnOnLandCollider(rate, std::nullopt, {.slope = true});
		Report("slope at the pivot", result);
		EXPECT_LT(result.largestMove, 5.0f);
		EXPECT_GE(result.lowest, -0.01f);
		EXPECT_TRUE(result.errors.empty());
	}
}

TEST(HandRigReplay, AutotestGlideAfterTurnDoesNotJumpAtThePress)
{
	// Camera test 3, glide-after-turn (2026-10-19, main b91aa07): at the middle press the arm touched 'MoATerrain'
	// 1.4 metres from the pivot, and the camera moved 56.7 metres in one frame.
	const auto result = ReplayOnLandCollider("camera-trace-autotest3-glide-after-turn.csv", k_LandLayer);
	Report("autotest 3 glide-after-turn", result);
	EXPECT_EQ(result.landHits, 0);
	EXPECT_LT(result.largestMove, 10.0f);
	EXPECT_GE(result.lowest, -0.01f);
	EXPECT_TRUE(result.errors.empty());
}

TEST(HandRigReplay, AutotestResetViewDoesNotJumpAtThePress)
{
	// Camera test 4, reset-view (2026-10-19, main 0ce86ef): at the middle press the land test of the arm stopped the
	// arm 1.9 metres from the pivot, where the line touched the slope of the land under the pivot. The camera moved
	// 51.7 metres in one frame.
	const auto result = ReplayOnLandCollider("camera-trace-autotest4-reset-view.csv", k_LandLayer);
	Report("autotest 4 reset-view", result);
	EXPECT_EQ(result.landHits, 0);
	EXPECT_LT(result.largestMove, 10.0f);
	EXPECT_GE(result.lowest, -0.01f);
	EXPECT_TRUE(result.errors.empty());
}

TEST(HandRigSpringArm, TheCameraStopsAtTheFootOfASteepSlope)
{
	constexpr float k_SlopeStage = 0.5f;
	// Camera test 3, rotate-pitch-cliff: after the input, the camera at the foot of a 34 metre slope moved up and
	// down by approximately 0.5 millimetres in each frame. The arm and the lift must stop.
	for (const auto rate : k_Rates)
	{
		SCOPED_TRACE(std::to_string(rate) + " fps");
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		CameraLoopHarness harness(ArmSettings(true));
		auto& game = harness.game;
		game.focusHeightEase = true;
		game.turnDegreesPerPixel = k_TurnDegreesPerPixel;
		game.mouse = {1100.0f, 300.0f, 0.0f};
		game.Start({}, k_SlopeStage);
		Idle(harness, 1.0f, deltaTime, nullptr);
		const auto start = game.transform.position;
		const auto focus = game.focus;
		// The land rises 34 metres in 8 metres, between the focus and the camera, 4 metres from the focus.
		const auto back = Normalize(WorldPoint {start.x - focus.x, 0.0f, start.z - focus.z});
		const auto foot = Dot(WorldPoint {focus.x, 0.0f, focus.z}, back) + 4.0f;
		game.ground = [back, foot](float x, float z) {
			const auto along = x * back.x + z * back.z - foot;
			return 100.0f + 34.0f * std::clamp(along / 8.0f, 0.0f, 1.0f);
		};
		game.buttons[2] = true;
		harness.Frame(deltaTime);
		// Flatter, and a part of a turn.
		for (int frame = 0; frame < 30; ++frame)
		{
			game.mouse.y += 4.0f;
			game.mouse.x += 2.0f;
			harness.Frame(deltaTime);
		}
		game.buttons[2] = false;
		Idle(harness, 3.0f, deltaTime, nullptr);
		float largestMove = 0.0f;
		auto previous = game.transform.position;
		for (int frame = 0; frame < 120; ++frame)
		{
			harness.Frame(deltaTime);
			largestMove = std::max(largestMove, Length(game.transform.position - previous));
			previous = game.transform.position;
		}
		const auto& trace = harness.loop.Trace();
		std::cout << rate << " fps: arm " << trace.armDistance.value_or(0.0f) << " m, lift " << trace.armLift.value_or(0.0f)
		          << " m, largest move after 3 s " << largestMove * 1000.0f << " mm" << std::endl;
		// After the gesture the land on the line no longer stops the arm (only the camera ball and the floor act), so the
		// camera can end clear of the slope with no lift. The rule of this test is that the camera stops.
		EXPECT_LT(largestMove, 1e-5f);
		EXPECT_TRUE(game.errors.empty());
	}
}
