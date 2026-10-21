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
#include <moa_mod/camera/CameraNav.h>

#include "CameraLoopHarness.h"

using namespace moa_mod;
using namespace moa_mod::camera;
using moa_mod::camera::test::CameraLoopHarness;
using moa_mod::camera::test::FakeCameraGame;

namespace
{

constexpr std::array k_Rates {30, 60, 144};

/// The settings of the hand rig with the navigation of batch C.
[[nodiscard]] moa_mod::camera::CameraSettings NavSettings()
{
	moa_mod::camera::CameraSettings settings;
	settings.enabled = true;
	settings.pitch = true;
	settings.lookUp = true;
	settings.pitchMode = PitchMode::Hand;
	return settings;
}

/// A harness on flat land at a zoom stage, settled for one second.
[[nodiscard]] CameraLoopHarness Settled(const moa_mod::camera::CameraSettings& settings, float stage, float deltaTime)
{
	CameraLoopHarness harness(settings);
	auto& game = harness.game;
	game.focusHeightEase = true;
	game.mouse = {1300.0f, 700.0f, 0.0f};
	game.Start({}, stage);
	for (float time = 0.0f; time < 1.0f; time += deltaTime)
	{
		harness.Frame(deltaTime);
	}
	return harness;
}

void RunFor(CameraLoopHarness& harness, float seconds, float deltaTime)
{
	for (float time = 0.0f; time < seconds; time += deltaTime)
	{
		harness.Frame(deltaTime);
	}
}

/// Two clicks of the left button at the place of the mouse: a press, a release and a press, one frame each.
void DoubleClick(CameraLoopHarness& harness, float deltaTime)
{
	auto& game = harness.game;
	game.buttons[0] = true;
	harness.Frame(deltaTime);
	game.buttons[0] = false;
	harness.Frame(deltaTime);
	game.buttons[0] = true;
	harness.Frame(deltaTime);
	game.buttons[0] = false;
}

[[nodiscard]] std::optional<WorldPoint> LandAtTheViewCentre(const FakeCameraGame& game)
{
	return game.LandAt(game.transform, {game.screenWidth * 0.5f, game.screenHeight * 0.5f, 0.0f});
}

/// The heading of the camera of the user, degrees.
[[nodiscard]] float YawOf(const FakeCameraGame& game)
{
	const auto forward = Rotate(game.transform.rotation, WorldPoint {0.0f, 0.0f, 1.0f});
	return std::atan2(forward.x, forward.z) / k_DegreesToRadians;
}

[[nodiscard]] float HorizontalDistance(WorldPoint a, WorldPoint b)
{
	return std::hypot(a.x - b.x, a.z - b.z);
}

} // namespace

TEST(SoftEdge, NeverPassesTheEdgeAndDoesNotDependOnTheSteps)
{
	for (const auto margin : {0.0f, 5.0f, 40.0f})
	{
		for (const auto start : {100.0f, 30.0f, 2.0f})
		{
			const auto once = SoftEdgeDistance(start, 150.0f, margin);
			auto steps = start;
			for (int step = 0; step < 1000; ++step)
			{
				steps = SoftEdgeDistance(steps, 0.15f, margin);
				EXPECT_GE(steps, 0.0f);
			}
			EXPECT_NEAR(once, steps, 1e-3f) << margin << " " << start;
			EXPECT_GE(once, 0.0f);
			if (margin > 0.0f)
			{
				EXPECT_GT(once, 0.0f);
			}
		}
	}
	// A move away from the edge is the full move.
	EXPECT_FLOAT_EQ(SoftEdgeDistance(10.0f, -5.0f, 40.0f), 15.0f);
	// Out of the margin, the move is the full move.
	EXPECT_FLOAT_EQ(SoftEdgeDistance(100.0f, 20.0f, 40.0f), 80.0f);
}

TEST(SoftEdge, TheSpeedHasNoStepAtTheMarginAndFallsToZero)
{
	// A constant move of 0.1 metre in each step toward the edge, from 60 metres, with a margin of 40 metres.
	const auto margin = 40.0f;
	auto distance = 60.0f;
	auto lastSpeed = 0.1f;
	float largestChange = 0.0f;
	for (int step = 0; step < 4000; ++step)
	{
		const auto next = SoftEdgeDistance(distance, 0.1f, margin);
		const auto speed = distance - next;
		EXPECT_LE(speed, lastSpeed + 1e-6f);
		largestChange = std::max(largestChange, lastSpeed - speed);
		lastSpeed = speed;
		distance = next;
	}
	EXPECT_LT(largestChange, 0.1f * 0.1f / margin * 1.5f);
	EXPECT_LT(lastSpeed, 1e-3f);
	EXPECT_GT(distance, 0.0f);
}

TEST(SoftEdge, TheBoxMovesOnlyTowardTheInsideFromOutside)
{
	const FocusBox box {.minimumX = -100.0f, .minimumZ = -100.0f, .maximumX = 100.0f, .maximumZ = 100.0f};
	const auto move = SoftEdgeMove({120.0f, 5.0f, 0.0f}, {10.0f, 1.0f, -10.0f}, box, 20.0f);
	EXPECT_EQ(move.x, 0.0f);
	EXPECT_EQ(move.y, 1.0f);
	EXPECT_EQ(move.z, -10.0f);
	const auto back = SoftEdgeMove({120.0f, 5.0f, 0.0f}, {-10.0f, 0.0f, 0.0f}, box, 20.0f);
	EXPECT_EQ(back.x, -10.0f);
	const auto clamped = ClampToBox({150.0f, 3.0f, -150.0f}, box);
	EXPECT_EQ(clamped.x, 100.0f);
	EXPECT_EQ(clamped.y, 3.0f);
	EXPECT_EQ(clamped.z, -100.0f);
}

TEST(CameraFlight, TheEaseStartsAndEndsWithNoSpeed)
{
	EXPECT_EQ(FlightEase(0.0f), 0.0f);
	EXPECT_EQ(FlightEase(1.0f), 1.0f);
	EXPECT_NEAR(FlightEase(0.5f), 0.5f, 1e-6f);
	EXPECT_LT(FlightEase(0.01f), 1e-5f);
	EXPECT_GT(FlightEase(0.99f), 1.0f - 1e-5f);
}

TEST(CameraSpots, TheJsonReadsBackTheSame)
{
	CameraSpotBook book;
	book["slot 2"][0] = CameraSpot {.position = {1.0f, 120.0f, -3.5f},
	                                .yaw = 45.0f,
	                                .pitch = 22.5f,
	                                .raise = -2.5f,
	                                .stage = 1.25f,
	                                .centre = {10.0f, 100.0f, 7.0f}};
	book["slot 2"][3] = CameraSpot {.position = {-1.0f, 300.0f, 2.0f},
	                                .yaw = -170.0f,
	                                .pitch = 50.0f,
	                                .raise = 0.0f,
	                                .stage = 3.0f,
	                                .centre = {0.0f, 90.0f, 0.0f}};
	book["default"][1] = book["slot 2"][0];
	const auto read = ParseCameraSpots(CameraSpotsJson(book).dump(2));
	ASSERT_TRUE(read) << read.GetError().message;
	EXPECT_EQ(*read, book);
	EXPECT_TRUE(ParseCameraSpots("")->empty());
	EXPECT_FALSE(ParseCameraSpots("[1, 2]"));
	EXPECT_FALSE(ParseCameraSpots(R"({"version": 2, "saves": {}})"));
	EXPECT_FALSE(ParseCameraSpots(R"({"version": 1, "saves": {"a": [{"yaw": 1}]}})"));
	EXPECT_FALSE(ParseCameraSpots(R"({"version": 1, "saves": {"a": [null, null, null, null, null]}})"));
}

TEST(CameraFlyTo, ADoubleClickOnTheLandPutsThePointAtTheViewCentre)
{
	std::optional<WorldPoint> firstEnd;
	for (const auto rate : k_Rates)
	{
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		auto harness = Settled(NavSettings(), 1.0f, deltaTime);
		auto& game = harness.game;
		const auto stage = game.stage;
		const auto yaw = YawOf(game);
		const auto centre = LandAtTheViewCentre(game);
		ASSERT_TRUE(centre);
		const auto distance = Length(*centre - game.transform.position);
		const auto target = game.LandAt(game.transform, game.mouse);
		ASSERT_TRUE(target);
		DoubleClick(harness, deltaTime);
		EXPECT_EQ(game.doubleClicks, 1);
		EXPECT_TRUE(harness.loop.Flying());
		EXPECT_LT(game.zoomToStage, 0.0f);
		// The flight eases: the first frames move little, and the camera arrives at rest.
		RunFor(harness, 0.7f, deltaTime);
		EXPECT_FALSE(harness.loop.Flying());
		const auto end = game.transform.position;
		RunFor(harness, 0.5f, deltaTime);
		const auto landed = LandAtTheViewCentre(game);
		ASSERT_TRUE(landed);
		std::cout << rate << " fps: target (" << target->x << ", " << target->z << "), view centre (" << landed->x << ", "
		          << landed->z << "), distance " << distance << " -> " << Length(*landed - game.transform.position)
		          << std::endl;
		EXPECT_LT(HorizontalDistance(*landed, *target), 0.05f) << rate;
		EXPECT_NEAR(Length(*landed - game.transform.position), distance, distance * 0.01f) << rate;
		EXPECT_NEAR(WrapDegrees(YawOf(game) - yaw), 0.0f, 0.01f) << rate;
		EXPECT_NEAR(game.stage, stage, 1e-4f) << rate;
		// The camera is at rest after the flight, and the end does not depend on the frame rate.
		EXPECT_LT(Length(game.transform.position - end), 1e-3f) << rate;
		if (!firstEnd)
		{
			firstEnd = end;
		}
		EXPECT_LT(Length(end - *firstEnd), 0.01f) << rate;
		// The flight sets the desired distance of the spring arm itself: the loop never corrects it.
		EXPECT_EQ(std::ranges::count_if(game.debugLines,
		                                [](const std::string& line) { return line.find("with no zoom") != std::string::npos; }),
		          0)
		    << rate;
		EXPECT_TRUE(game.errors.empty());
	}
}

TEST(CameraFlyTo, TheFlightHasNoJumpAndStopsAtAnInput)
{
	const auto deltaTime = 1.0f / 60.0f;
	auto harness = Settled(NavSettings(), 1.0f, deltaTime);
	auto& game = harness.game;
	game.mouse = {300.0f, 900.0f, 0.0f};
	DoubleClick(harness, deltaTime);
	ASSERT_TRUE(harness.loop.Flying());
	// The largest move of a frame is near the middle of the flight; the first frames move little.
	float largest = 0.0f;
	float first = -1.0f;
	for (int frame = 0; frame < 15; ++frame)
	{
		harness.Frame(deltaTime);
		const auto move = harness.ShownMove();
		if (first < 0.0f)
		{
			first = move;
		}
		largest = std::max(largest, move);
	}
	EXPECT_LT(first, largest * 0.2f);
	// A press of the middle button stops the flight. The camera does not jump.
	game.buttons[2] = true;
	harness.Frame(deltaTime);
	EXPECT_FALSE(harness.loop.Flying());
	EXPECT_TRUE(harness.loop.Trace().flightStopped);
	EXPECT_LT(harness.ShownMove(), largest);
	game.buttons[2] = false;
	const auto stopped = game.transform.position;
	RunFor(harness, 1.0f, deltaTime);
	EXPECT_LT(Length(game.transform.position - stopped), 0.05f);
	EXPECT_TRUE(game.errors.empty());
}

TEST(CameraFlyTo, ADoubleClickOnAnObjectOrWithTheSettingOffIsTheGame)
{
	const auto deltaTime = 1.0f / 60.0f;
	{
		auto harness = Settled(NavSettings(), 1.0f, deltaTime);
		auto& game = harness.game;
		game.clickOnObject = [](WorldPoint) { return true; };
		const auto start = game.transform.position;
		DoubleClick(harness, deltaTime);
		RunFor(harness, 1.0f, deltaTime);
		EXPECT_EQ(game.doubleClicks, 0);
		EXPECT_FALSE(harness.loop.Flying());
		EXPECT_LT(Length(game.transform.position - start), 1e-3f);
	}
	{
		auto settings = NavSettings();
		settings.flyTo = false;
		auto harness = Settled(settings, 2.0f, deltaTime);
		auto& game = harness.game;
		DoubleClick(harness, deltaTime);
		EXPECT_FALSE(harness.loop.Flying());
		RunFor(harness, 3.0f, deltaTime);
		// The game zooms to stage 1 from above stage 1.1, and the rig follows the zoom of the game.
		EXPECT_EQ(game.doubleClicks, 1);
		EXPECT_GT(game.stage, 0.8f);
		EXPECT_LT(game.stage, 1.2f);
	}
}

TEST(CameraFlyTo, TheSettingOfTheStageZoomsDuringTheFlight)
{
	const auto deltaTime = 1.0f / 60.0f;
	auto settings = NavSettings();
	settings.flyToStage = 2.0;
	// At stage 0.5 the zoom of the double click of the game has nothing to do, so the game gives no zoom to find.
	auto harness = Settled(settings, 0.3f, deltaTime);
	auto& game = harness.game;
	const auto target = game.LandAt(game.transform, game.mouse);
	ASSERT_TRUE(target);
	DoubleClick(harness, deltaTime);
	RunFor(harness, 1.0f, deltaTime);
	EXPECT_NEAR(game.stage, 2.0f, 1e-4f);
	const auto landed = LandAtTheViewCentre(game);
	ASSERT_TRUE(landed);
	EXPECT_LT(HorizontalDistance(*landed, *target), 0.1f);
	// The view is the camera of the game at stage 2: the height of the row above the point, and the tilt of the row.
	EXPECT_NEAR(game.transform.position.y - target->y, FakeCameraGame::k_StageTable[2].height, 0.5f);
	EXPECT_TRUE(game.errors.empty());
}

TEST(CameraSpotsFlight, AFlightToASavedSpotGivesTheSavedView)
{
	for (const auto rate : k_Rates)
	{
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		auto harness = Settled(NavSettings(), 1.0f, deltaTime);
		auto& game = harness.game;
		// A pitch, so the spot has a raise.
		game.buttons[2] = true;
		harness.Frame(deltaTime);
		for (int frame = 0; frame < 20; ++frame)
		{
			game.mouse.y += 3.0f;
			harness.Frame(deltaTime);
		}
		game.buttons[2] = false;
		RunFor(harness, 1.0f, deltaTime);
		const auto spot = harness.loop.CurrentSpot();
		ASSERT_TRUE(spot);
		const auto saved = game.transform.position;
		const auto savedRotation = game.transform.rotation;
		// Go away: a turn of the keys, a pan and a zoom.
		game.turnPerSecond = 90.0f;
		game.pan = {40.0f, 0.0f, -25.0f};
		RunFor(harness, 1.0f, deltaTime);
		game.turnPerSecond = 0.0f;
		game.pan = {};
		game.wheel = -0.1f;
		harness.Frame(deltaTime);
		game.wheel = 0.0f;
		RunFor(harness, 1.0f, deltaTime);
		ASSERT_GT(Length(game.transform.position - saved), 10.0f);
		ASSERT_TRUE(harness.loop.FlyToSpot(*spot));
		RunFor(harness, 1.0f, deltaTime);
		const auto turn = Multiply(game.transform.rotation, Inverse(savedRotation));
		std::cout << rate << " fps: back at the spot within " << Length(game.transform.position - saved) << " m" << std::endl;
		// Near the land (at 144 fps the pitch puts the camera 2 m above the land), the land ball pushes the camera, and
		// the arm keeps the pushed camera at the desired distance from its pivot. The flight has another pivot, so the
		// camera can be a few centimetres from the saved camera.
		EXPECT_LT(Length(game.transform.position - saved), 0.1f) << rate;
		EXPECT_GT(std::abs(turn.w), 1.0f - 1e-6f) << rate;
		const auto again = harness.loop.CurrentSpot();
		ASSERT_TRUE(again);
		EXPECT_NEAR(again->stage, spot->stage, 1e-4f);
		EXPECT_NEAR(again->raise, spot->raise, 1e-3f);
		EXPECT_TRUE(game.errors.empty());
	}
}

TEST(CameraReset, TheResetGivesTheNorthAndTheTiltOfTheStage)
{
	const auto deltaTime = 1.0f / 60.0f;
	for (const auto north : {0.0f, 40.0f, -120.0f})
	{
		auto harness = Settled(NavSettings(), 1.5f, deltaTime);
		auto& game = harness.game;
		game.buttons[2] = true;
		harness.Frame(deltaTime);
		for (int frame = 0; frame < 20; ++frame)
		{
			game.mouse.y -= 4.0f;
			game.mouse.x += 5.0f;
			harness.Frame(deltaTime);
		}
		game.buttons[2] = false;
		RunFor(harness, 1.0f, deltaTime);
		const auto centre = LandAtTheViewCentre(game);
		ASSERT_TRUE(centre);
		const auto stage = game.stage;
		ASSERT_TRUE(harness.loop.ResetView(north));
		RunFor(harness, 1.0f, deltaTime);
		const auto forward = Rotate(game.transform.rotation, WorldPoint {0.0f, 0.0f, 1.0f});
		const auto tilt = -std::asin(forward.y) / k_DegreesToRadians;
		const auto row = [&] {
			const auto low = static_cast<size_t>(stage);
			const auto part = stage - static_cast<float>(low);
			const auto& a = FakeCameraGame::k_StageTable[low];
			const auto& b = FakeCameraGame::k_StageTable[std::min(low + 1, FakeCameraGame::k_StageTable.size() - 1)];
			return FakeCameraGame::StageRow {a.height + (b.height - a.height) * part, a.tilt + (b.tilt - a.tilt) * part};
		}();
		const auto landed = LandAtTheViewCentre(game);
		ASSERT_TRUE(landed);
		EXPECT_NEAR(WrapDegrees(YawOf(game) - north), 0.0f, 0.01f) << north;
		EXPECT_NEAR(tilt, row.tilt, 0.01f) << north;
		EXPECT_NEAR(game.stage, stage, 1e-4f);
		EXPECT_LT(HorizontalDistance(*landed, *centre), 0.05f);
		// The camera of the game at this stage: the height of the row above the point at the view centre.
		EXPECT_NEAR(game.transform.position.y - centre->y, row.height, 0.05f) << north;
		EXPECT_TRUE(game.errors.empty());
	}
}

TEST(CameraSoftEdge, AKeyPanStopsSoftlyAtTheEdgeOfTheMap)
{
	struct Result
	{
		float largestX = -1e9f;
		float largestSpeedIncrease = 0.0f; ///< In the margin, metres for each second.
		float endX = 0.0f;
		float largestStep = 0.0f; ///< The largest change of the speed from one frame to the next, metres per second.
	};
	auto run = [](float deltaTime, double margin) {
		auto settings = NavSettings();
		settings.edgeMargin = margin;
		// The chase also makes the hard edge soft. This test compares the soft edge with the hard stop of the game, so
		// the chase is off.
		settings.chaseSeconds = 0.0;
		CameraLoopHarness harness(settings);
		auto& game = harness.game;
		game.mapMinimum = -200.0f;
		game.mapMaximum = 100.0f;
		game.direction = 90.0f; // The camera looks along +x.
		game.Start({}, 1.0f);
		RunFor(harness, 0.5f, deltaTime);
		Result result;
		game.panKeys = {.x = 0.0f, .y = 1.0f};
		auto lastX = game.focus.x;
		std::optional<float> lastSpeed;
		for (float time = 0.0f; time < 8.0f; time += deltaTime)
		{
			harness.Frame(deltaTime);
			result.largestX = std::max(result.largestX, game.focus.x);
			const auto speed = (game.focus.x - lastX) / deltaTime;
			if (lastSpeed && time > 2.0f)
			{
				// After the keys reach their speed, the speed must only fall.
				result.largestSpeedIncrease = std::max(result.largestSpeedIncrease, speed - *lastSpeed);
				result.largestStep = std::max(result.largestStep, std::abs(speed - *lastSpeed));
			}
			lastSpeed = speed;
			lastX = game.focus.x;
		}
		result.endX = game.focus.x;
		EXPECT_TRUE(game.errors.empty());
		return result;
	};
	const auto hard = run(1.0f / 60.0f, 0.0);
	for (const auto rate : k_Rates)
	{
		const auto soft = run(1.0f / static_cast<float>(rate), 0.25);
		std::cout << rate << " fps: soft edge end x " << soft.endX << ", largest speed step " << soft.largestStep
		          << " m/s; hard edge largest speed step " << hard.largestStep << " m/s" << std::endl;
		EXPECT_LE(soft.largestX, 100.0f) << rate;
		EXPECT_GT(soft.endX, 99.0f) << rate;
		EXPECT_LT(soft.largestSpeedIncrease, 0.5f) << rate;
		// The hard stop of the game drops the full speed in one frame; the soft edge spreads it.
		EXPECT_LT(soft.largestStep, hard.largestStep * 0.25f) << rate;
	}
	EXPECT_LE(hard.largestX, 100.0f);
}

TEST(CameraAvoidance, AKeyPanPastATreeNeverGoesIntoItAndComesBackOut)
{
	for (const auto rate : k_Rates)
	{
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		// The object collision is on: the old rule, where the spring arm stops in front of the tree.
		auto settings = NavSettings();
		settings.objectCollision = true;
		auto harness = Settled(settings, 0.0f, deltaTime);
		auto& game = harness.game;
		const auto start = Length(game.transform.position - game.focus);
		// A tree between the camera and the focus, a few metres to the right: a pan to the right moves the line through
		// it.
		const auto camera = game.transform.position;
		const auto right = Rotate(game.transform.rotation, WorldPoint {1.0f, 0.0f, 0.0f});
		const auto middle = camera + (game.focus - camera) * 0.5f;
		game.obstacles.push_back({.centre = middle + right * 6.0f, .radius = 1.0f});
		game.panKeys = {.x = 1.0f, .y = 0.0f};
		float nearestObstacle = 1e9f;
		float nearest = 1e9f;
		float largestDesiredChange = 0.0f;
		std::optional<float> desired;
		for (float time = 0.0f; time < 1.5f; time += deltaTime)
		{
			harness.Frame(deltaTime);
			nearestObstacle = std::min(nearestObstacle, game.ObstacleClearance(game.transform.position));
			nearest = std::min(nearest, Length(game.transform.position - game.focus));
			if (const auto value = harness.loop.Trace().desiredDistance)
			{
				if (desired)
				{
					largestDesiredChange = std::max(largestDesiredChange, std::abs(*value - *desired));
				}
				desired = value;
			}
		}
		game.panKeys = {};
		RunFor(harness, 2.0f, deltaTime);
		const auto end = Length(game.transform.position - game.focus);
		std::cout << rate << " fps: start " << start << " m, nearest " << nearest << " m, end " << end
		          << " m, nearest obstacle " << nearestObstacle << " m" << std::endl;
		EXPECT_GE(nearestObstacle, 0.0f) << rate;
		EXPECT_LT(nearest, start * 0.9f) << rate;
		EXPECT_NEAR(end, start, start * 0.02f) << rate;
		EXPECT_LT(largestDesiredChange, 0.01f) << rate;
		EXPECT_TRUE(game.errors.empty());
	}
}

TEST(CameraAvoidance, AKeyPanOntoAHillRisesSmoothlyAndNeverGoesIntoTheLand)
{
	struct Result
	{
		float lowest = 1e9f;      ///< The lowest height above the exact land.
		float largestRise = 0.0f; ///< The largest rise of the camera in one frame, metres.
	};
	auto run = [](bool smooth) {
		const auto deltaTime = 1.0f / 60.0f;
		auto settings = NavSettings();
		settings.panAvoidance = smooth;
		settings.springArm = true;
		CameraLoopHarness harness(settings);
		auto& game = harness.game;
		game.direction = 0.0f; // The camera looks along +z.
		// A steep bank in front of the camera: the land rises 12 metres in 2 metres.
		game.ground = [](float, float z) { return 100.0f + 12.0f * std::clamp((z - 3.0f) / 2.0f, 0.0f, 1.0f); };
		game.focusHeightEase = true;
		game.Start({0.0f, 0.0f, -20.0f}, 0.0f);
		RunFor(harness, 0.5f, deltaTime);
		Result result;
		game.panKeys = {.x = 0.0f, .y = 1.0f};
		auto lastY = game.transform.position.y;
		for (float time = 0.0f; time < 3.0f; time += deltaTime)
		{
			harness.Frame(deltaTime);
			const auto& position = game.transform.position;
			result.lowest = std::min(result.lowest, position.y - game.GroundAt(position.x, position.z));
			result.largestRise = std::max(result.largestRise, position.y - lastY);
			lastY = position.y;
		}
		EXPECT_TRUE(game.errors.empty());
		return result;
	};
	const auto atOnce = run(false);
	const auto smooth = run(true);
	std::cout << "hill: largest rise in a frame " << atOnce.largestRise << " m at once, " << smooth.largestRise
	          << " m smooth; lowest above the land height " << atOnce.lowest << " m, " << smooth.lowest << " m" << std::endl;
	// The floor: the clearance above the exact land.
	EXPECT_GE(smooth.lowest, 1.5f - 1e-3f);
	// The key pan carries the pivot of the spring arm onto the bank (12 m in 2 m) in one frame, and the arm then goes
	// from the top of the bank (`KeepArmPivotOnLand`). Before that, the pivot stayed under the bank and the arm pulled
	// the camera down to the clearance, with rises of 7.5 m in a frame both ways. Now the camera stays higher, and the
	// largest rise is 3.4 m at once and 5.0 m smooth. So the limit is the old rise, not the rise at once.
	EXPECT_LT(smooth.largestRise, 7.5f);
	EXPECT_LT(atOnce.largestRise, 7.5f);
}
