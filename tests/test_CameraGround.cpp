/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

// The ground look of batch D: below the closest zoom stage the camera goes down to the eye height above the land,
// the view becomes level, the camera turns about itself and the key pan walks on the land.

#include <cmath>

#include <algorithm>
#include <array>
#include <iostream>
#include <optional>
#include <vector>

#include <core/CameraGround.h>
#include <gtest/gtest.h>

#include "CameraLoopHarness.h"

using namespace moa_mod;
using namespace moa_mod::camera;
using moa_mod::camera::test::CameraLoopHarness;
using moa_mod::camera::test::FakeCameraGame;

namespace
{

constexpr std::array k_Rates {30, 60, 144};
constexpr float k_Eye = 1.8f;

const std::vector<StageView> k_Table {{6.0f, 20.0f}, {30.0f, 25.0f}, {90.0f, 45.0f}, {200.0f, 50.0f}, {1200.0f, 50.0f}};

/// The settings of the hand rig with the pitch and the look up on, as the user plays.
[[nodiscard]] moa_mod::camera::CameraSettings GroundSettings()
{
	moa_mod::camera::CameraSettings settings;
	settings.enabled = true;
	settings.pitchMode = PitchMode::Hand;
	settings.pitch = true;
	settings.lookUp = true;
	settings.groundLook = true;
	settings.groundEyeHeight = k_Eye;
	return settings;
}

[[nodiscard]] float Hills(float x, float z)
{
	return 100.0f + 3.0f * std::sin(x * 0.05f) + 2.0f * std::cos(z * 0.07f);
}

[[nodiscard]] float ZoomValue(const CameraLoopHarness& harness)
{
	return harness.loop.State()["zoomValue"].get<float>();
}

void RunFor(CameraLoopHarness& harness, float seconds, float deltaTime)
{
	const auto frames = static_cast<int>(std::lround(seconds / deltaTime));
	for (int frame = 0; frame < frames; ++frame)
	{
		harness.Frame(deltaTime);
	}
}

/// The angle between two rotations, degrees. The half chord gives small angles with no loss of precision.
[[nodiscard]] float TurnBetween(Quaternion a, Quaternion b)
{
	const auto sign = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0.0f ? -1.0 : 1.0;
	const auto dx = static_cast<double>(a.x) - sign * b.x;
	const auto dy = static_cast<double>(a.y) - sign * b.y;
	const auto dz = static_cast<double>(a.z) - sign * b.z;
	const auto dw = static_cast<double>(a.w) - sign * b.w;
	const auto chord = std::sqrt(dx * dx + dy * dy + dz * dz + dw * dw);
	return static_cast<float>(4.0 * std::asin(std::min(chord * 0.5, 1.0)) / static_cast<double>(k_DegreesToRadians));
}

/// The angle of the view centre above the horizon, degrees.
[[nodiscard]] float ViewElevation(const CameraLoopHarness& harness)
{
	const auto forward = Rotate(harness.game.transform.rotation, WorldPoint {0.0f, 0.0f, 1.0f});
	return std::asin(std::clamp(forward.y, -1.0f, 1.0f)) / k_DegreesToRadians;
}

/// A harness with the camera settled at a zoom stage over the land.
[[nodiscard]] CameraLoopHarness Settled(const moa_mod::camera::CameraSettings& settings, float stage, float deltaTime,
                                        bool hills = false)
{
	CameraLoopHarness harness(settings);
	harness.game.mapMinimum = -1e6f;
	harness.game.mapMaximum = 1e6f;
	if (hills)
	{
		harness.game.ground = Hills;
	}
	harness.game.mouse = {960.0f, 700.0f, 0.0f};
	harness.game.Start({0.0f, 0.0f, 0.0f}, stage);
	RunFor(harness, 1.0f, deltaTime);
	return harness;
}

/// Turn the wheel by a count of notches, one notch in each 0.12 seconds. More than zero zooms in.
void Notches(CameraLoopHarness& harness, int count, float deltaTime, const std::function<void()>& eachFrame = {})
{
	const auto framesPerNotch = std::max(1, static_cast<int>(std::lround(0.12f / deltaTime)));
	for (int notch = 0; notch < std::abs(count); ++notch)
	{
		for (int frame = 0; frame < framesPerNotch; ++frame)
		{
			harness.game.wheel = frame == 0 ? (count > 0 ? 0.1f : -0.1f) : 0.0f;
			harness.Frame(deltaTime);
			if (eachFrame)
			{
				eachFrame();
			}
		}
	}
	harness.game.wheel = 0.0f;
}

/// The camera position and rotation of each frame, to find a jump.
struct MotionWatch
{
	std::vector<float> moves;
	std::vector<float> turns;
	std::vector<float> stages;
	CameraPlacement last;
	bool started = false;

	void Add(const CameraLoopHarness& harness)
	{
		const auto& now = harness.game.transform;
		if (started)
		{
			moves.push_back(Length(now.position - last.position));
			turns.push_back(TurnBetween(now.rotation, last.rotation));
			stages.push_back(ZoomValue(harness));
		}
		last = now;
		started = true;
	}

	/// The largest ratio of the change of one frame to the larger change of the two frames next to it. A jump gives a
	/// large ratio; a smooth motion gives about 1.
	[[nodiscard]] static float Spike(const std::vector<float>& values, float floor)
	{
		float largest = 0.0f;
		for (size_t index = 1; index + 1 < values.size(); ++index)
		{
			const auto next = std::max(values[index - 1], values[index + 1]);
			largest = std::max(largest, values[index] / (next + floor));
		}
		return largest;
	}
};

} // namespace

TEST(CameraGroundMaths, TheGroundStagesContinueTheHeightOfTheTable)
{
	// Stage 0 has the height of the first row, stage -1 the eye height, and a stage is the reverse of its height.
	EXPECT_NEAR(HeightOfStage(k_Table, 0.0f, k_Eye), 6.0f, 1e-5f);
	EXPECT_NEAR(HeightOfStage(k_Table, -1.0f, k_Eye), k_Eye, 1e-5f);
	for (float stage = -1.0f; stage <= 1.0f; stage += 0.05f)
	{
		EXPECT_NEAR(StageOfHeight(k_Table, HeightOfStage(k_Table, stage, k_Eye), k_Eye), stage, 1e-4f) << stage;
	}
	// One notch changes the height by the ratio in the ground range and across stage 0.
	const StageLimits limits {.minimum = -1.0f, .maximum = 4.0f};
	for (float stage = -0.7f; stage <= 0.2f; stage += 0.1f)
	{
		const auto in = StageAfterNotches(k_Table, stage, 1.0f, 1.25f, limits, k_Eye);
		EXPECT_NEAR(HeightOfStage(k_Table, stage, k_Eye) / HeightOfStage(k_Table, in, k_Eye), 1.25f, 1e-3f) << stage;
	}
	// With no ground height, the old rule stops at stage 0.
	EXPECT_EQ(StageOfHeight(k_Table, 3.0f), 0.0f);
	EXPECT_EQ(StageAfterNotches(k_Table, 0.0f, 1.0f, 1.25f, limits), 0.0f);
}

TEST(CameraGroundMaths, TheRangeAndTheTiltHaveNoStepAtTheClosestStage)
{
	const PitchRange detail {.steepest = -65.0f, .flattest = 15.0f, .highest = 30.0f};
	const auto top = GroundPitchRange(detail, 20.0f, 0.0f, {});
	EXPECT_NEAR(top.steepest, detail.steepest, 1e-4f);
	EXPECT_NEAR(top.flattest, detail.flattest, 1e-4f);
	EXPECT_NEAR(top.highest, detail.highest, 1e-4f);
	EXPECT_NEAR(GroundTilt(20.0f, 0.0f), 20.0f, 1e-5f);
	// On the land the view goes from 60 degrees down to 60 degrees up about a level view.
	const auto bottom = GroundPitchRange(detail, 20.0f, 1.0f, {.down = 60.0f, .up = 60.0f});
	EXPECT_NEAR(GroundTilt(20.0f, 1.0f), 0.0f, 1e-5f);
	EXPECT_NEAR(bottom.steepest, -60.0f, 1e-4f);
	EXPECT_NEAR(bottom.highest, 60.0f, 1e-4f);
	// The range is never empty and always holds a raise of zero.
	for (float part = 0.0f; part <= 1.0f; part += 0.05f)
	{
		const auto range = GroundPitchRange(detail, 20.0f, part, {});
		EXPECT_LE(range.steepest, 0.0f);
		EXPECT_GE(range.flattest, 0.0f);
		EXPECT_GE(range.highest, range.flattest);
	}
}

TEST(CameraGroundMaths, TheHeightAndItsReverseAgree)
{
	for (float part = 0.0f; part < 0.99f; part += 0.1f)
	{
		const auto height = GroundHeight(7.5f, k_Eye, part);
		ASSERT_TRUE(GroundTopHeight(height, k_Eye, part));
		EXPECT_NEAR(*GroundTopHeight(height, k_Eye, part), 7.5f, 1e-3f) << part;
	}
	EXPECT_NEAR(GroundHeight(7.5f, k_Eye, 1.0f), k_Eye, 1e-5f);
	EXPECT_FALSE(GroundTopHeight(k_Eye, k_Eye, 1.0f));
	// A higher land takes effect at once, a lower land smoothly.
	EXPECT_EQ(FollowLand(100.0f, 101.0f, 0.016f, 0.3f), 101.0f);
	const auto lower = FollowLand(100.0f, 99.0f, 0.016f, 0.3f);
	EXPECT_LT(lower, 100.0f);
	EXPECT_GT(lower, 99.0f);
}

TEST(CameraGround, TheZoomIntoTheGroundAndBackHasNoJump)
{
	for (const auto rate : k_Rates)
	{
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		auto harness = Settled(GroundSettings(), 0.3f, deltaTime);
		MotionWatch watch;
		watch.Add(harness);
		// The pivot of each notch above stage 0 stays at its place on the screen.
		std::optional<WorldPoint> pivot;
		std::optional<Vector2> pivotScreen;
		float largestDrift = 0.0f;
		const auto check = [&] {
			watch.Add(harness);
			const auto& trace = harness.loop.Trace();
			if (trace.pivot && (!pivot || Length(*trace.pivot - *pivot) > 1e-4f))
			{
				pivot = trace.pivot;
				pivotScreen.reset();
			}
			if (!pivot)
			{
				return;
			}
			auto screen = ScreenPoint(harness.game.ScreenCameraOf(harness.game.transform), *pivot);
			if (!screen)
			{
				return;
			}
			if (trace.pivotKept && ZoomValue(harness) > 0.0f && pivotScreen)
			{
				largestDrift = std::max(largestDrift, std::hypot(screen->x - pivotScreen->x, screen->y - pivotScreen->y));
			}
			else
			{
				pivotScreen = *screen;
			}
		};
		Notches(harness, 12, deltaTime, check);
		for (int frame = 0; frame < rate * 2; ++frame)
		{
			harness.Frame(deltaTime);
			check();
		}
		EXPECT_TRUE(harness.loop.InGround()) << rate;
		Notches(harness, -12, deltaTime, check);
		for (int frame = 0; frame < rate * 2; ++frame)
		{
			harness.Frame(deltaTime);
			check();
		}
		EXPECT_FALSE(harness.loop.InGround()) << rate;
		const auto crossings = std::ranges::count_if(watch.stages, [](float stage) { return stage < 0.0f; });
		EXPECT_GT(crossings, 0) << rate;
		// No jump of the position or of the rotation in any frame, also where the zoom crosses stage 0.
		std::cout << rate << " fps: frames below stage 0 " << crossings << ", move spike "
		          << MotionWatch::Spike(watch.moves, 2e-3f) << ", turn spike " << MotionWatch::Spike(watch.turns, 2e-2f)
		          << ", pivot drift above stage 0 " << largestDrift << " px\n";
		EXPECT_LT(MotionWatch::Spike(watch.moves, 2e-3f), 2.5f) << rate;
		EXPECT_LT(MotionWatch::Spike(watch.turns, 2e-2f), 2.5f) << rate;
		EXPECT_LE(largestDrift, 0.5f) << rate;
		EXPECT_TRUE(harness.game.errors.empty()) << rate;
	}
}

TEST(CameraGround, TheBottomOfTheRangeIsALevelViewAtTheEyeHeight)
{
	for (const auto rate : k_Rates)
	{
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		auto harness = Settled(GroundSettings(), 0.0f, deltaTime, true);
		float lowest = 1e9f;
		Notches(harness, 10, deltaTime, [&] {
			const auto& camera = harness.game.transform.position;
			lowest = std::min(lowest, camera.y - (harness.game.GroundAt(camera.x, camera.z) + k_Eye));
		});
		RunFor(harness, 2.0f, deltaTime);
		const auto& camera = harness.game.transform.position;
		EXPECT_NEAR(ZoomValue(harness), -1.0f, 1e-3f) << rate;
		EXPECT_NEAR(ViewElevation(harness), 0.0f, 0.01f) << rate;
		// The land under the camera is the highest land near it.
		EXPECT_NEAR(camera.y - (harness.game.HighestGroundAtForTest(camera) - 2.0f), k_Eye, 0.01f) << rate;
		EXPECT_GE(lowest, -0.01f) << rate;
		// The game stays at its closest stage, and its state places its camera at the camera of the user.
		EXPECT_EQ(harness.game.stage, 0.0f) << rate;
		const auto placed =
		    harness.game.focus + GameCameraOffset(harness.game.direction, harness.game.height, harness.game.tilt);
		EXPECT_LT(Length(placed - camera), 1e-2f) << rate;
		// The game did not push its camera above the land in the frame.
		EXPECT_TRUE(harness.loop.Trace().gamePushIgnored) << rate;
	}
}

TEST(CameraGround, TheZoomOutReturnsToTheNormalRig)
{
	for (const auto rate : k_Rates)
	{
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		auto harness = Settled(GroundSettings(), 0.3f, deltaTime);
		const auto start = harness.game.transform;
		int corrections = 0;
		const auto count = [&] { corrections += harness.loop.Trace().desiredChange == 4 ? 1 : 0; };
		Notches(harness, 7, deltaTime, count);
		RunFor(harness, 2.0f, deltaTime);
		ASSERT_TRUE(harness.loop.InGround()) << rate;
		EXPECT_LT(ZoomValue(harness), -0.2f) << rate;
		Notches(harness, -7, deltaTime, count);
		RunFor(harness, 2.0f, deltaTime);
		EXPECT_FALSE(harness.loop.InGround()) << rate;
		EXPECT_NEAR(ZoomValue(harness), 0.3f, 1e-3f) << rate;
		EXPECT_LT(Length(harness.game.transform.position - start.position), 0.01f) << rate;
		EXPECT_LT(TurnBetween(harness.game.transform.rotation, start.rotation), 0.01f) << rate;

		// Above the closest stage the hand rig works as before: a turn of the middle button keeps the desired distance.
		harness.game.turnDegreesPerPixel = 0.2f;
		harness.game.buttons[2] = true;
		for (int frame = 0; frame < rate; ++frame)
		{
			harness.game.mouse.x += 300.0f * deltaTime;
			harness.Frame(deltaTime);
			count();
		}
		harness.game.buttons[2] = false;
		RunFor(harness, 1.0f, deltaTime);
		EXPECT_EQ(corrections, 0) << rate;
		EXPECT_TRUE(harness.game.errors.empty()) << rate;
	}
}

TEST(CameraGround, ALookUpShowsTheSkyAndTheCameraStaysInPlace)
{
	for (const auto rate : k_Rates)
	{
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		auto harness = Settled(GroundSettings(), 0.0f, deltaTime);
		Notches(harness, 10, deltaTime);
		RunFor(harness, 2.0f, deltaTime);
		const auto standing = harness.game.transform.position;
		harness.game.buttons[2] = true;
		for (int frame = 0; frame < rate * 2; ++frame)
		{
			harness.game.mouse.y += 600.0f * deltaTime;
			harness.Frame(deltaTime);
		}
		harness.game.buttons[2] = false;
		RunFor(harness, 1.0f, deltaTime);
		EXPECT_NEAR(ViewElevation(harness), 60.0f, 0.1f) << rate;
		EXPECT_LT(Length(harness.game.transform.position - standing), 0.01f) << rate;
		// And down to the limit below the horizon.
		harness.game.buttons[2] = true;
		for (int frame = 0; frame < rate * 4; ++frame)
		{
			harness.game.mouse.y -= 600.0f * deltaTime;
			harness.Frame(deltaTime);
		}
		harness.game.buttons[2] = false;
		RunFor(harness, 1.0f, deltaTime);
		EXPECT_NEAR(ViewElevation(harness), -60.0f, 0.1f) << rate;
		EXPECT_LT(Length(harness.game.transform.position - standing), 0.01f) << rate;
		EXPECT_TRUE(harness.game.errors.empty()) << rate;
	}
}

TEST(CameraGround, TheTurnOnTheLandIsAboutTheCamera)
{
	auto harness = Settled(GroundSettings(), 0.0f, 1.0f / 60.0f);
	Notches(harness, 10, 1.0f / 60.0f);
	RunFor(harness, 2.0f, 1.0f / 60.0f);
	const auto standing = harness.game.transform.position;
	harness.game.turnPerSecond = 90.0f;
	RunFor(harness, 2.0f, 1.0f / 60.0f);
	harness.game.turnPerSecond = 0.0f;
	RunFor(harness, 1.0f, 1.0f / 60.0f);
	EXPECT_LT(Length(harness.game.transform.position - standing), 0.01f);
}

TEST(CameraGround, TheWalkFollowsTheLandAndNeverGoesIntoAnObstacle)
{
	for (const auto rate : k_Rates)
	{
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		// The object collision is on: the walk stops in front of the objects and slides along them.
		auto settings = GroundSettings();
		settings.objectCollision = true;
		auto harness = Settled(settings, 0.0f, deltaTime, true);
		Notches(harness, 10, deltaTime);
		RunFor(harness, 2.0f, deltaTime);
		// A building and a tree on the way of the walk.
		const auto camera = harness.game.transform.position;
		const auto forward = Rotate(harness.game.transform.rotation, WorldPoint {0.0f, 0.0f, 1.0f});
		const auto ahead = Normalize(WorldPoint {forward.x, 0.0f, forward.z});
		const auto wall = camera + ahead * 15.0f;
		harness.game.boxes.push_back(
		    {.minimum = {wall.x - 4.0f, 80.0f, wall.z - 4.0f}, .maximum = {wall.x + 4.0f, 120.0f, wall.z + 4.0f}});
		const auto tree = camera + ahead * 6.0f + WorldPoint {ahead.z, 0.0f, -ahead.x} * 3.0f;
		harness.game.obstacles.push_back({.centre = {tree.x, Hills(tree.x, tree.z) + 2.0f, tree.z}, .radius = 0.5f});

		float lowest = 1e9f;
		float nearest = 1e9f;
		float fastest = 0.0f;
		auto last = camera;
		const auto watch = [&] {
			const auto& now = harness.game.transform.position;
			lowest = std::min(lowest, now.y - (harness.game.GroundAt(now.x, now.z) + k_Eye));
			nearest = std::min(nearest, harness.game.ObstacleClearance(now));
			fastest = std::max(fastest, std::hypot(now.x - last.x, now.z - last.z) / deltaTime);
			last = now;
		};
		// Walk forward into the building, then turn and walk along it.
		harness.game.panKeys = {.x = 0.0f, .y = 1.0f};
		for (int frame = 0; frame < rate * 8; ++frame)
		{
			harness.Frame(deltaTime);
			watch();
		}
		harness.game.panKeys = {.x = 1.0f, .y = 1.0f};
		harness.game.turnPerSecond = 20.0f;
		for (int frame = 0; frame < rate * 6; ++frame)
		{
			harness.Frame(deltaTime);
			watch();
		}
		harness.game.panKeys = {};
		harness.game.turnPerSecond = 0.0f;
		RunFor(harness, 1.0f, deltaTime);
		std::cout << rate << " fps: walk " << Length(harness.game.transform.position - camera)
		          << " m, lowest above the eye height " << lowest << " m, nearest obstacle " << nearest << " m, fastest "
		          << fastest << " m/s\n";
		EXPECT_GE(lowest, -0.01f) << rate;
		EXPECT_GE(nearest, FakeCameraGame::k_CollisionRadius - 1e-3f) << rate;
		// The camera walked to the building and stopped in front of it.
		EXPECT_GT(Length(harness.game.transform.position - camera), 8.0f) << rate;
		EXPECT_LT(fastest, 4.0f * 1.5f + 1.0f) << rate;
		EXPECT_TRUE(harness.loop.InGround()) << rate;
		EXPECT_TRUE(harness.game.errors.empty()) << rate;
	}
}

TEST(CameraGround, TheWalkHasTheWalkSpeed)
{
	auto harness = Settled(GroundSettings(), 0.0f, 1.0f / 60.0f);
	Notches(harness, 10, 1.0f / 60.0f);
	RunFor(harness, 2.0f, 1.0f / 60.0f);
	harness.game.panKeys = {.x = 0.0f, .y = 1.0f};
	RunFor(harness, 3.0f, 1.0f / 60.0f);
	const auto from = harness.game.transform.position;
	RunFor(harness, 1.0f, 1.0f / 60.0f);
	const auto speed = std::hypot(harness.game.transform.position.x - from.x, harness.game.transform.position.z - from.z);
	EXPECT_NEAR(speed, 4.0f, 0.2f);
}

TEST(CameraGround, TheLandDragMovesTheCameraAndTheSkyDoesNotThrowIt)
{
	for (const auto rate : k_Rates)
	{
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		auto harness = Settled(GroundSettings(), 0.0f, deltaTime);
		Notches(harness, 10, deltaTime);
		RunFor(harness, 2.0f, deltaTime);
		const auto start = harness.game.transform.position;
		float largest = 0.0f;
		auto last = start;
		const auto watch = [&] {
			largest = std::max(largest, Length(harness.game.transform.position - last));
			last = harness.game.transform.position;
		};
		// Grab the land below the horizon and pull it toward the camera.
		harness.game.mouse = {960.0f, 300.0f, 0.0f};
		harness.Frame(deltaTime);
		last = harness.game.transform.position;
		harness.game.buttons[1] = true;
		for (int frame = 0; frame < rate; ++frame)
		{
			harness.game.mouse.y -= 100.0f * deltaTime;
			harness.Frame(deltaTime);
			watch();
		}
		const auto moved = Length(harness.game.transform.position - start);
		EXPECT_GT(moved, 0.5f) << rate;
		// Move the cursor above the horizon: the camera does not jump.
		for (int frame = 0; frame < rate; ++frame)
		{
			harness.game.mouse.y += 800.0f * deltaTime;
			harness.Frame(deltaTime);
			watch();
		}
		harness.game.buttons[1] = false;
		RunFor(harness, 1.0f, deltaTime);
		watch();
		std::cout << rate << " fps: land drag " << moved << " m, largest move in a frame " << largest << " m\n";
		EXPECT_LT(largest, 3.0f) << rate;
		const auto& camera = harness.game.transform.position;
		EXPECT_GE(camera.y - (harness.game.GroundAt(camera.x, camera.z) + k_Eye), -0.01f) << rate;
		EXPECT_TRUE(harness.game.errors.empty()) << rate;
	}
}

TEST(CameraGround, WithTheSettingOffTheZoomStopsAtTheClosestStage)
{
	auto settings = GroundSettings();
	settings.groundLook = false;
	auto harness = Settled(settings, 0.0f, 1.0f / 60.0f);
	Notches(harness, 10, 1.0f / 60.0f);
	RunFor(harness, 2.0f, 1.0f / 60.0f);
	EXPECT_FALSE(harness.loop.InGround());
	EXPECT_NEAR(ZoomValue(harness), 0.0f, 1e-4f);
}
