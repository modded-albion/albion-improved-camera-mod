/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <cmath>

#include <algorithm>
#include <array>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CameraLoopHarness.h"

namespace moa_mod::camera::test
{
namespace
{

constexpr std::array k_Rates {30, 60, 144};
/// The land under the camera of the tests, metres.
constexpr float k_Base = 100.0f;
/// The default clearance above the land, metres.
constexpr float k_Clearance = 1.5f;

/// The settings of the hand rig with the spring arm and the defaults of the land: no object collision, the camera
/// ball and the eased push.
[[nodiscard]] moa_mod::camera::CameraSettings LandSettings()
{
	moa_mod::camera::CameraSettings settings;
	settings.enabled = true;
	settings.pitch = true;
	settings.lookUp = true;
	settings.pitchMode = PitchMode::Hand;
	settings.springArm = true;
	KeepTheFeelOfTheOldTests(settings);
	settings.panAvoidance = true;
	settings.pitchSensitivity = 30.0;
	return settings;
}

/// A harness on flat land at a stage, settled for one second.
[[nodiscard]] CameraLoopHarness Settled(const moa_mod::camera::CameraSettings& settings, float stage, float deltaTime)
{
	CameraLoopHarness harness(settings);
	harness.game.focusHeightEase = true;
	harness.game.Start({}, stage);
	for (float time = 0.0f; time < 1.0f; time += deltaTime)
	{
		harness.Frame(deltaTime);
	}
	return harness;
}

void RunFor(CameraLoopHarness& harness, float seconds, float deltaTime, const std::function<void()>& each = {})
{
	for (float time = 0.0f; time < seconds; time += deltaTime)
	{
		harness.Frame(deltaTime);
		if (each)
		{
			each();
		}
	}
}

/// The height of the camera above the land of the model.
[[nodiscard]] float AboveLand(const FakeCameraGame& game)
{
	const auto& camera = game.transform.position;
	return camera.y - game.GroundAt(camera.x, camera.z);
}

[[nodiscard]] WorldPoint Horizontal(WorldPoint point)
{
	return Normalize(WorldPoint {point.x, 0.0f, point.z});
}

} // namespace

// The pure parts.

TEST(CameraLand, TheArmSamplesAreNearAtTheHandAndFartherApartNearTheCamera)
{
	// The distance from the hand to the camera at Map.
	constexpr float k_Map = 1566.0f;
	const auto distances = ArmSampleDistances(k_Map, {});
	ASSERT_FALSE(distances.empty());
	EXPECT_LE(distances.size(), 256U);
	EXPECT_NEAR(distances.front(), 0.5f, 1e-4f);
	EXPECT_FLOAT_EQ(distances.back(), k_Map);
	float largest = 0.0f;
	float previous = 0.0f;
	for (const auto distance : distances)
	{
		EXPECT_GT(distance, previous);
		const auto step = distance - previous;
		largest = std::max(largest, step);
		if (distance < 5.0f)
		{
			EXPECT_LE(step, 0.61f);
		}
		previous = distance;
	}
	// The old arm had 32 samples at Map: 49 metres apart.
	std::cout << "Map: " << distances.size() << " samples, largest step " << largest << " m (old: 32 samples, " << k_Map / 32.0f
	          << " m)" << std::endl;
	EXPECT_LT(largest, k_Map / 32.0f);

	// The maximum count: each step becomes longer by the same factor, and the last sample is at the camera.
	const auto few = ArmSampleDistances(k_Map, {.firstStep = 0.5f, .growth = 0.02f, .maximumPoints = 32});
	EXPECT_LE(few.size(), 32U);
	EXPECT_FLOAT_EQ(few.back(), k_Map);
	const auto shortArm = ArmSampleDistances(0.3f, {});
	ASSERT_EQ(shortArm.size(), 1U);
	EXPECT_FLOAT_EQ(shortArm.front(), 0.3f);
	EXPECT_TRUE(ArmSampleDistances(0.0f, {}).empty());
}

TEST(CameraLand, TheLandNormalIsTheNormalOfAPlane)
{
	auto plane = [](float x, float z) -> std::optional<float> { return k_Base + 0.5f * x + 0.25f * z; };
	const auto normal = LandNormal(plane, 3.0f, -7.0f);
	ASSERT_TRUE(normal);
	const auto expected = Normalize(WorldPoint {-0.5f, 1.0f, -0.25f});
	EXPECT_NEAR(normal->x, expected.x, 1e-4f);
	EXPECT_NEAR(normal->y, expected.y, 1e-4f);
	EXPECT_NEAR(normal->z, expected.z, 1e-4f);
}

TEST(CameraLand, TheBallPushesACameraUpFromFlatLandAndThenStops)
{
	auto flat = [](float, float) -> std::optional<float> { return k_Base; };
	const auto pushed = PushOutOfLand(flat, {0.0f, k_Base + 1.0f, 0.0f}, Quaternion {}, 2.0f, 3);
	ASSERT_TRUE(pushed);
	EXPECT_NEAR(pushed->position.y, k_Base + 2.0f, 1e-4f);
	EXPECT_NEAR(pushed->position.x, 0.0f, 1e-5f);
	EXPECT_GE(pushed->pushes, 1);
	// A camera that the ball pushed out does not move again.
	const auto again = PushOutOfLand(flat, pushed->position, Quaternion {}, 2.0f, 3);
	ASSERT_TRUE(again);
	EXPECT_EQ(again->pushes, 0);
	EXPECT_EQ(again->position.y, pushed->position.y);
}

TEST(CameraLand, TheBallPushesACameraOutOfASteepSlopeAlongTheNormal)
{
	// A slope of 72 degrees: the land rises 3 metres for each metre in x.
	auto slope = [](float x, float) -> std::optional<float> { return k_Base + 3.0f * x; };
	const WorldPoint camera {0.0f, k_Base + 1.0f, 0.0f};
	const auto normal = Normalize(WorldPoint {-3.0f, 1.0f, 0.0f});
	const auto first = PushOutOfLand(slope, camera, Quaternion {}, 2.0f, 3);
	const auto second = PushOutOfLand(slope, camera, Quaternion {}, 2.0f, 3);
	ASSERT_TRUE(first && second);
	// The same camera gives the same push, so a camera that does not move does not shake.
	EXPECT_EQ(first->position.x, second->position.x);
	EXPECT_EQ(first->position.y, second->position.y);
	const auto push = first->position - camera;
	EXPECT_GT(Dot(Normalize(push), normal), 0.99f);
	// The distance from the camera to the slope (it was 0.32 metres) is now near the radius of the ball.
	const auto distance = Dot(first->position - WorldPoint {0.0f, k_Base, 0.0f}, normal);
	std::cout << "slope: the camera is " << distance << " m from the land after " << first->pushes << " pushes" << std::endl;
	EXPECT_GT(distance, 1.5f);
}

TEST(CameraLand, TheEaseOfThePushHasALimitedStepAndGetsToItsGoal)
{
	for (const auto rate : k_Rates)
	{
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		WorldPoint push {};
		const WorldPoint goal {0.0f, 5.0f, 0.0f};
		float largest = 0.0f;
		for (float time = 0.0f; time < 1.0f; time += deltaTime)
		{
			const auto next = EasePush(push, goal, deltaTime, 0.1f, 0.4f, 30.0f);
			largest = std::max(largest, Length(next - push));
			push = next;
		}
		EXPECT_LE(largest, 30.0f * deltaTime + 1e-5f) << rate;
		EXPECT_LT(Length(push - goal), 1e-3f) << rate;
		// The return is slower: after 0.1 seconds approximately half of the push is still there (95 percent goes in
		// 0.4 seconds).
		for (float time = 0.0f; time < 0.1f - 1e-4f; time += deltaTime)
		{
			push = EasePush(push, {}, deltaTime, 0.1f, 0.4f, 30.0f);
		}
		EXPECT_GT(push.y, 2.0f) << rate;
		// A time of zero moves at once.
		EXPECT_EQ(EasePush({}, goal, deltaTime, 0.0f, 0.4f, 30.0f).y, goal.y);
	}
}

// The spring arm on the land.

TEST(CameraLand, ARidgeBetweenTheHandAndTheCameraAtMapStopsTheArm)
{
	for (const auto rate : k_Rates)
	{
		SCOPED_TRACE(std::to_string(rate) + " fps");
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		// The focus is the pivot, so that the ridge is on the line from the pivot to the camera in the gesture below.
		auto settings = LandSettings();
		settings.pivotOnHand = false;
		auto harness = Settled(settings, 4.0f, deltaTime);
		auto& game = harness.game;
		const auto focus = game.focus;
		const auto camera = game.transform.position;
		const auto length = Length(camera - focus);
		const auto line = (camera - focus) * (1.0f / length);
		const auto across = Horizontal(camera - focus);
		const auto horizontalLength = Dot(camera - focus, across);
		// A ridge across the line, with sides that rise 2 metres for each metre, 15 metres over the line at its top. The
		// part over the line is approximately 36 metres long, between two samples of the old arm (32 samples at Map,
		// 49 metres apart).
		const auto ridgeAlong = length * 12.75f / 32.0f;
		const auto ridgeAt = Dot(line * ridgeAlong, across);
		const auto ridgeTop = (focus + line * ridgeAlong).y - k_Base + 15.0f;
		constexpr float k_Side = 2.0f;
		game.ground = [=](float x, float z) {
			const auto along = Dot(WorldPoint {x - focus.x, 0.0f, z - focus.z}, across);
			return k_Base + std::max(0.0f, ridgeTop - k_Side * std::abs(along - ridgeAt));
		};
		// The old land test: 32 samples on the line with the land height of the game (the highest land near the point
		// plus 2 metres), and a point under that height less 2 metres. It does not find the ridge.
		int oldHits = 0;
		for (int index = 1; index <= 32; ++index)
		{
			const auto point = focus + line * (length * static_cast<float>(index) / 32.0f);
			if (game.HighestGroundAtForTest(point) - 2.0f > point.y)
			{
				++oldHits;
			}
		}
		EXPECT_EQ(oldHits, 0);
		ASSERT_GT(horizontalLength, ridgeAt + ridgeTop / k_Side);
		// The land on the line stops the arm only in a gesture about the pivot (with no gesture, only the camera ball and
		// the floor keep the camera above the land). The middle button held with no move is a gesture.
		game.buttons[2] = true;
		RunFor(harness, 2.0f, deltaTime);
		const auto& trace = harness.loop.Trace();
		const auto shown = Length(game.transform.position - focus);
		std::cout << rate << " fps: ridge at " << ridgeAlong << " m, the camera at " << shown << " m from the hand, "
		          << trace.armLandSamples << " land samples" << std::endl;
		EXPECT_TRUE(trace.armHit.has_value());
		EXPECT_LT(shown, ridgeAlong);
		EXPECT_GE(AboveLand(game), k_Clearance - 1e-3f);
		EXPECT_EQ(game.casts, 0);
		EXPECT_TRUE(game.errors.empty());
	}
}

namespace
{

/// The result of a camera next to a steep slope.
struct SlopeResult
{
	WorldPoint start;           ///< The camera before the slope.
	WorldPoint pushed;          ///< The camera at the end of the slope part.
	WorldPoint end;             ///< The camera after the slope went away.
	WorldPoint normal;          ///< The normal of the slope.
	WorldPoint footPoint;       ///< A point of the plane of the slope.
	float largestMove = 0.0f;   ///< The largest move of the camera in one frame while the slope pushes it.
	float lastMove = 0.0f;      ///< The largest move of the camera in one frame in the last half second.
	float lowest = 1e9f;        ///< The lowest height of the camera above the land.
	float desiredChange = 0.0f; ///< The largest change of the desired distance.
	float startDistance = 0.0f; ///< The distance from the focus to the camera at the start.
	float endDistance = 0.0f;   ///< The distance from the focus to the camera at the end.
	int pushes = 0;             ///< The points of the ball that pushed the camera in the last frame of the slope.
};

/// A camera at Detail, then a slope of 72 degrees that starts half a metre to the left of the camera and rises to
/// the right, for 3 seconds. Then the slope goes away.
[[nodiscard]] SlopeResult NextToASlope(int rate)
{
	const auto deltaTime = 1.0f / static_cast<float>(rate);
	auto harness = Settled(LandSettings(), 0.0f, deltaTime);
	auto& game = harness.game;
	SlopeResult result;
	result.start = game.transform.position;
	result.startDistance = Length(result.start - game.focus);
	const auto right = Horizontal(Rotate(game.transform.rotation, WorldPoint {1.0f, 0.0f, 0.0f}));
	const auto foot = Dot(result.start, right) - 0.5f;
	game.ground = [=](float x, float z) { return k_Base + 3.0f * std::max(0.0f, Dot(WorldPoint {x, 0.0f, z}, right) - foot); };
	result.normal = Normalize(WorldPoint {0.0f, 1.0f, 0.0f} - right * 3.0f);
	result.footPoint = right * foot + WorldPoint {0.0f, k_Base, 0.0f};
	std::optional<float> desired;
	auto last = game.transform.position;
	const auto watch = [&](float time, float seconds) {
		const auto move = Length(game.transform.position - last);
		last = game.transform.position;
		result.largestMove = std::max(result.largestMove, move);
		if (time > seconds - 0.5f)
		{
			result.lastMove = std::max(result.lastMove, move);
		}
		result.lowest = std::min(result.lowest, AboveLand(game));
		if (const auto value = harness.loop.Trace().desiredDistance)
		{
			if (desired)
			{
				result.desiredChange = std::max(result.desiredChange, std::abs(*value - *desired));
			}
			else
			{
				desired = value;
			}
		}
	};
	constexpr float k_SlopeSeconds = 3.0f;
	for (float time = 0.0f; time < k_SlopeSeconds; time += deltaTime)
	{
		harness.Frame(deltaTime);
		watch(time, k_SlopeSeconds);
	}
	result.pushed = game.transform.position;
	result.pushes = harness.loop.Trace().landPushes;
	game.ground = [](float, float) { return k_Base; };
	RunFor(harness, 2.0f, deltaTime);
	result.end = game.transform.position;
	result.endDistance = Length(result.end - game.focus);
	EXPECT_TRUE(game.errors.empty());
	return result;
}

} // namespace

TEST(CameraLand, ACameraNextToASteepSlopeMovesOutAlongTheNormalWithNoShake)
{
	std::optional<WorldPoint> first;
	for (const auto rate : k_Rates)
	{
		SCOPED_TRACE(std::to_string(rate) + " fps");
		const auto result = NextToASlope(rate);
		const auto push = result.pushed - result.start;
		// The distance from the camera to the plane of the slope.
		const auto distance = Dot(result.pushed - result.footPoint, result.normal);
		std::cout << rate << " fps: push " << Length(push) << " m, along the normal " << Dot(Normalize(push), result.normal)
		          << ", " << distance << " m from the slope, largest step " << result.largestMove << " m, last steps "
		          << result.lastMove << " m, lowest " << result.lowest << " m, back within "
		          << Length(result.end - result.start) << " m" << std::endl;
		// The camera moves away from the slope, mostly along its normal, to near the radius of the ball (2 metres).
		EXPECT_GT(Length(push), 0.1f);
		EXPECT_GT(distance, 1.5f);
		EXPECT_GT(Dot(Normalize(push), result.normal), 0.8f);
		// No shake: the camera stops.
		EXPECT_LT(result.lastMove, 1e-4f);
		// The ease: no move in one frame is larger than the speed limit of the push.
		EXPECT_LE(result.largestMove, 30.0f / static_cast<float>(rate) + 1e-3f);
		EXPECT_GE(result.lowest, k_Clearance - 1e-3f);
		// The push does not change the desired distance, and the camera goes back when the slope is not there.
		EXPECT_LT(result.desiredChange, 1e-3f);
		EXPECT_LT(Length(result.end - result.start), 1e-2f);
		EXPECT_NEAR(result.endDistance, result.startDistance, 1e-2f);
		// The same push at each frame rate.
		if (!first)
		{
			first = result.pushed;
		}
		EXPECT_LT(Length(result.pushed - *first), 1e-2f);
	}
}

TEST(CameraLand, LandThatRisesUnderTheCameraLiftsItAtOnceToTheFloor)
{
	for (const auto rate : k_Rates)
	{
		SCOPED_TRACE(std::to_string(rate) + " fps");
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		auto harness = Settled(LandSettings(), 0.0f, deltaTime);
		auto& game = harness.game;
		const auto camera = game.transform.position;
		// A hill under the camera that comes to half a metre under it in one frame.
		const auto top = camera.y - k_Base - 0.5f;
		game.ground = [=](float x, float z) {
			return k_Base + top * std::max(0.0f, 1.0f - std::hypot(x - camera.x, z - camera.z) / 4.0f);
		};
		float lowest = 1e9f;
		RunFor(harness, 1.0f, deltaTime, [&] { lowest = std::min(lowest, AboveLand(game)); });
		EXPECT_GE(lowest, k_Clearance - 1e-3f);
		EXPECT_TRUE(game.errors.empty());
	}
}

TEST(CameraLand, WithNoObjectCollisionTheCameraGoesIntoABuilding)
{
	for (const auto rate : k_Rates)
	{
		SCOPED_TRACE(std::to_string(rate) + " fps");
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		for (const auto objects : {false, true})
		{
			auto settings = LandSettings();
			settings.objectCollision = objects;
			auto harness = Settled(settings, 0.0f, deltaTime);
			auto& game = harness.game;
			const auto camera = game.transform.position;
			const auto start = Length(camera - game.focus);
			// A building around the camera.
			game.boxes.push_back(
			    {.minimum = camera - WorldPoint {3.0f, 8.0f, 3.0f}, .maximum = camera + WorldPoint {3.0f, 3.0f, 3.0f}});
			game.casts = 0;
			float clearance = 1e9f;
			RunFor(harness, 1.5f, deltaTime,
			       [&] { clearance = std::min(clearance, game.ObstacleClearance(game.transform.position)); });
			const auto end = Length(game.transform.position - game.focus);
			if (objects)
			{
				// The old rule: the spring arm stops in front of the building.
				EXPECT_GT(game.casts, 0);
				EXPECT_LT(end, start * 0.9f);
			}
			else
			{
				// No sphere test, and the camera stays in the building, at its distance.
				EXPECT_EQ(game.casts, 0);
				EXPECT_LT(clearance, 0.0f);
				EXPECT_NEAR(end, start, 1e-3f);
			}
			EXPECT_TRUE(game.errors.empty());
		}
	}
}

// The return from a stand-back.

namespace
{

/// The result of a stand-back.
struct HandBackResult
{
	float raiseBefore = 0.0f;    ///< The raise of the user before the stand-back, degrees.
	float raiseAfter = 0.0f;     ///< The raise after the return, degrees.
	float pitchAfter = 0.0f;     ///< The pitch of the camera after the return, degrees.
	float stageTilt = 0.0f;      ///< The tilt of the stage of the game after the return, degrees.
	float fromGameCamera = 0.0f; ///< The distance from the camera after the return to the camera of the game state.
	float fromBefore = 0.0f;     ///< The distance from the camera after the return to the camera before the stand-back.
	bool told = false;           ///< The loop told the return from the state of the game.
};

/// A pitch at stage 1, then a stand-back of half a second. In a cutscene the game moves the camera and turns it down;
/// in a pause it does nothing. Then the mod is active again for `afterSeconds`.
[[nodiscard]] HandBackResult StandBack(int rate, bool cutscene, bool flyBack, float afterSeconds)
{
	const auto deltaTime = 1.0f / static_cast<float>(rate);
	auto settings = LandSettings();
	settings.flyBackAfterStandBack = flyBack;
	settings.pitchSmoothing = 0.0;
	auto harness = Settled(settings, 1.0f, deltaTime);
	auto& game = harness.game;
	// The pitch of the user: a middle button drag up.
	game.buttons[2] = true;
	harness.Frame(deltaTime);
	for (int frame = 0; frame < 10; ++frame)
	{
		game.mouse.y += 4.0f;
		harness.Frame(deltaTime);
	}
	game.buttons[2] = false;
	RunFor(harness, 1.0f, deltaTime);
	HandBackResult result;
	result.raiseBefore = harness.loop.Trace().raise;
	const auto before = game.transform.position;
	for (float time = 0.0f; time < 0.5f; time += deltaTime)
	{
		harness.seconds += deltaTime;
		harness.loop.StandBack();
		if (cutscene)
		{
			// The game runs its camera, and the cutscene moves the camera and turns it 25 degrees down.
			game.RunFrame(deltaTime);
			game.transform.position = game.transform.position + WorldPoint {4.0f, -2.0f, 3.0f};
			game.transform.rotation = Multiply(game.transform.rotation, PitchRotation(25.0f));
		}
		game.EndFrame();
	}
	RunFor(harness, afterSeconds, deltaTime);
	const auto& trace = harness.loop.Trace();
	result.raiseAfter = trace.raise;
	result.pitchAfter = PitchOf(game.transform.rotation);
	std::vector<StageView> table;
	for (const auto& stageRow : FakeCameraGame::k_StageTable)
	{
		table.push_back({.height = stageRow.height, .tilt = stageRow.tilt});
	}
	const auto row = InterpolateStage(table, game.stage);
	result.stageTilt = row.tilt;
	result.fromGameCamera =
	    Length(game.transform.position - (game.focus + GameCameraOffset(game.direction, row.height, row.tilt)));
	result.fromBefore = Length(game.transform.position - before);
	result.told = std::ranges::any_of(game.events, [](const auto& event) {
		return event.first == CameraEventKind::StandBack && event.second == "hand-back from the state of the game";
	});
	EXPECT_TRUE(game.errors.empty());
	return result;
}

} // namespace

TEST(CameraLand, AfterACutsceneTheCameraStartsFromTheStateOfTheGame)
{
	for (const auto rate : k_Rates)
	{
		SCOPED_TRACE(std::to_string(rate) + " fps");
		const auto result = StandBack(rate, true, false, 0.5f);
		std::cout << rate << " fps: raise " << result.raiseBefore << " -> " << result.raiseAfter << " deg, pitch "
		          << result.pitchAfter << " deg (stage tilt " << result.stageTilt << "), " << result.fromGameCamera
		          << " m from the camera of the game" << std::endl;
		ASSERT_GT(std::abs(result.raiseBefore), 1.0f);
		EXPECT_TRUE(result.told);
		// The view of the game: no raise, the tilt of the stage, not the tilt of the cutscene.
		EXPECT_NEAR(result.raiseAfter, 0.0f, 1e-3f);
		EXPECT_NEAR(result.pitchAfter, result.stageTilt, 0.05f);
		EXPECT_LT(result.fromGameCamera, 0.05f);
	}
}

TEST(CameraLand, AfterAPauseTheCameraKeepsTheViewOfTheUser)
{
	for (const auto rate : k_Rates)
	{
		SCOPED_TRACE(std::to_string(rate) + " fps");
		const auto result = StandBack(rate, false, false, 0.5f);
		EXPECT_FALSE(result.told);
		EXPECT_NEAR(result.raiseAfter, result.raiseBefore, 1e-3f);
		EXPECT_LT(result.fromBefore, 1e-3f);
	}
}

TEST(CameraLand, WithTheSettingTheCameraFliesBackAfterACutscene)
{
	for (const auto rate : k_Rates)
	{
		SCOPED_TRACE(std::to_string(rate) + " fps");
		const auto result = StandBack(rate, true, true, 2.0f);
		std::cout << rate << " fps: back within " << result.fromBefore << " m, raise " << result.raiseAfter << " deg (was "
		          << result.raiseBefore << ")" << std::endl;
		EXPECT_TRUE(result.told);
		EXPECT_LT(result.fromBefore, 0.05f);
		EXPECT_NEAR(result.raiseAfter, result.raiseBefore, 0.05f);
	}
}

} // namespace moa_mod::camera::test
