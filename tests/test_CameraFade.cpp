/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <cmath>

#include <array>
#include <vector>

#include <gtest/gtest.h>
#include <moa_mod/camera/CameraFade.h>

#include "FadeHarness.h"

namespace
{

using moa_mod::WorldPoint;
using moa_mod::camera::FadeSettings;
using moa_mod::camera::FadeView;
using moa_mod::camera::OccluderFade;
using moa_mod::camera::test::FakeFadeScene;

constexpr std::array k_Rates {30, 60, 144};

/// A building from z = -5 to z = 5, and a tree beside it.
struct World
{
	FakeFadeScene scene;
	uint64_t building = 0;
	uint64_t tree = 0;
	World()
	{
		building = scene.Add({-5.0f, 100.0f, -5.0f}, {5.0f, 115.0f, 5.0f});
		tree = scene.Add({20.0f, 100.0f, -1.0f}, {22.0f, 110.0f, 1.0f});
	}
};

/// The camera 2 m from the wall of the building: nearer than the fade distance of 4 m.
constexpr FadeView k_Near {.camera = {0.0f, 110.0f, -7.0f}};
/// The camera 25 m behind the building, which is between the camera and a hand at z = 30.
constexpr FadeView k_Behind {.camera = {0.0f, 110.0f, -30.0f}};
constexpr FadeView k_Clear {.camera = {0.0f, 150.0f, -30.0f}};

TEST(OccluderFade, FadesAnObjectNearTheCameraInTheSameTimeAtEachFrameRate)
{
	const FadeSettings settings {.enabled = true, .opacity = 0.3f, .seconds = 0.25f, .hideInside = true};
	for (const auto rate : k_Rates)
	{
		SCOPED_TRACE(std::to_string(rate) + " fps");
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		World world;
		OccluderFade fade;
		// After half the fade time, the opacity is half way.
		for (int frame = 0; frame < rate / 2 / 4 * 2 + 0; ++frame)
		{
			fade.Step(world.scene, settings, k_Near, deltaTime);
		}
		const auto halfTime = static_cast<float>(rate / 2 / 4 * 2) * deltaTime;
		EXPECT_NEAR(world.scene.Get(world.building).opacity, 1.0f - 0.7f * halfTime / 0.25f, 0.011f);
		for (int frame = 0; frame < rate; ++frame)
		{
			fade.Step(world.scene, settings, k_Near, deltaTime);
		}
		EXPECT_FLOAT_EQ(world.scene.Get(world.building).opacity, 0.3f);
		EXPECT_FALSE(world.scene.Get(world.building).hidden);
		// The tree is not near the camera.
		EXPECT_EQ(world.scene.Get(world.tree).changes, 0);
		EXPECT_EQ(fade.Faded(), 1);
		EXPECT_EQ(fade.Hidden(), 0);
		ASSERT_TRUE(fade.LowestOpacity());
		EXPECT_FLOAT_EQ(*fade.LowestOpacity(), 0.3f);
	}
}

TEST(OccluderFade, FadesBackInWhenNoLongerNearAndGivesTheObjectBack)
{
	const FadeSettings settings {.enabled = true, .opacity = 0.3f, .seconds = 0.25f, .hideInside = true};
	for (const auto rate : k_Rates)
	{
		SCOPED_TRACE(std::to_string(rate) + " fps");
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		World world;
		OccluderFade fade;
		for (int frame = 0; frame < rate; ++frame)
		{
			fade.Step(world.scene, settings, k_Near, deltaTime);
		}
		// Away for a short time: the object stays faded, so that it does not flicker.
		fade.Step(world.scene, settings, k_Clear, deltaTime);
		EXPECT_FLOAT_EQ(world.scene.Get(world.building).opacity, 0.3f);
		float time = deltaTime;
		while (!fade.Empty() && time < 2.0f)
		{
			fade.Step(world.scene, settings, k_Clear, deltaTime);
			time += deltaTime;
		}
		EXPECT_TRUE(fade.Empty());
		// The delay of 0.15 s and the fade in of 0.25 s.
		EXPECT_NEAR(time, 0.4f, 2.0f * deltaTime);
		EXPECT_FLOAT_EQ(world.scene.Get(world.building).opacity, 1.0f);
		EXPECT_EQ(world.scene.Get(world.building).restores, 1);
		EXPECT_EQ(world.scene.Changed(), 0);
	}
}

TEST(OccluderFade, HidesAnObjectThatTheCameraIsInAtOnce)
{
	const FadeSettings settings {.enabled = true, .opacity = 0.3f, .seconds = 0.25f, .hideInside = true};
	World world;
	OccluderFade fade;
	const FadeView inside {.camera = {0.0f, 110.0f, 0.0f}};
	fade.Step(world.scene, settings, inside, 1.0f / 60.0f);
	EXPECT_TRUE(world.scene.Get(world.building).hidden);
	EXPECT_EQ(fade.Hidden(), 1);
	// Out of the object, it fades back in from the fade opacity.
	for (int frame = 0; frame < 60; ++frame)
	{
		fade.Step(world.scene, settings, k_Clear, 1.0f / 60.0f);
	}
	EXPECT_EQ(world.scene.Changed(), 0);
	EXPECT_TRUE(fade.Empty());
	// With the setting off, the camera in the object hides nothing.
	FadeSettings noHide = settings;
	noHide.hideInside = false;
	fade.Step(world.scene, noHide, inside, 1.0f / 60.0f);
	EXPECT_FALSE(world.scene.Get(world.building).hidden);
}

TEST(OccluderFade, GivesEachObjectBackAtTheEnd)
{
	const FadeSettings settings {.enabled = true, .opacity = 0.0f, .seconds = 0.1f, .hideInside = true};
	World world;
	OccluderFade fade;
	for (int frame = 0; frame < 30; ++frame)
	{
		fade.Step(world.scene, settings, k_Near, 1.0f / 60.0f);
	}
	// An opacity of zero hides the object.
	EXPECT_TRUE(world.scene.Get(world.building).hidden);
	fade.RestoreAll(world.scene);
	EXPECT_EQ(world.scene.Changed(), 0);
	EXPECT_TRUE(fade.Empty());
	// The setting off gives each object back too.
	for (int frame = 0; frame < 30; ++frame)
	{
		fade.Step(world.scene, settings, k_Near, 1.0f / 60.0f);
	}
	FadeSettings off = settings;
	off.enabled = false;
	fade.Step(world.scene, off, k_Near, 1.0f / 60.0f);
	EXPECT_EQ(world.scene.Changed(), 0);
	EXPECT_TRUE(fade.Empty());
}

TEST(OccluderFade, ForgetsAnObjectThatIsGoneAndFailsClosed)
{
	const FadeSettings settings {.enabled = true, .opacity = 0.3f, .seconds = 0.25f, .hideInside = true};
	World world;
	OccluderFade fade;
	fade.Step(world.scene, settings, k_Near, 1.0f / 60.0f);
	EXPECT_EQ(fade.Faded(), 1);
	world.scene.Get(world.building).gone = true;
	fade.Step(world.scene, settings, k_Near, 1.0f / 60.0f);
	EXPECT_TRUE(fade.Empty());
	// The tests of the scene fail: nothing new fades, and the fade tells it.
	World other;
	other.scene.lookupFails = true;
	OccluderFade failed;
	failed.Step(other.scene, settings, k_Near, 1.0f / 60.0f);
	EXPECT_TRUE(failed.Empty());
	EXPECT_TRUE(failed.LookupFailed());
}

TEST(OccluderFade, DoesNotFadeAnObjectBetweenTheCameraAndTheHandThatIsNotNear)
{
	const FadeSettings settings {.enabled = true, .opacity = 0.3f, .seconds = 0.25f, .hideInside = true};
	World world;
	OccluderFade fade;
	for (int frame = 0; frame < 60; ++frame)
	{
		fade.Step(world.scene, settings, k_Behind, 1.0f / 60.0f);
	}
	EXPECT_EQ(world.scene.Get(world.building).changes, 0);
	EXPECT_TRUE(fade.Empty());
}

TEST(OccluderFade, FadesOnlyNearerThanTheFadeDistance)
{
	FadeSettings settings {.enabled = true, .opacity = 0.3f, .seconds = 0.25f, .distance = 1.5f, .hideInside = true};
	World world;
	OccluderFade fade;
	// The wall is 2 m from the camera: not near with a fade distance of 1.5 m.
	for (int frame = 0; frame < 30; ++frame)
	{
		fade.Step(world.scene, settings, k_Near, 1.0f / 60.0f);
	}
	EXPECT_EQ(world.scene.Get(world.building).changes, 0);
	settings.distance = 2.5f;
	for (int frame = 0; frame < 30; ++frame)
	{
		fade.Step(world.scene, settings, k_Near, 1.0f / 60.0f);
	}
	EXPECT_FLOAT_EQ(world.scene.Get(world.building).opacity, 0.3f);
}

TEST(OccluderFade, NeverFadesOrHidesAnObjectInTheHand)
{
	const FadeSettings settings {.enabled = true, .opacity = 0.3f, .seconds = 0.25f, .hideInside = true};
	World world;
	// A boulder in the hand, around the camera: the worst place, where an object is near and the camera is in it.
	const auto boulder = world.scene.Add({-1.0f, 109.0f, -8.0f}, {1.0f, 111.0f, -6.0f});
	world.scene.Get(boulder).held = true;
	OccluderFade fade;
	for (int frame = 0; frame < 60; ++frame)
	{
		fade.Step(world.scene, settings, k_Near, 1.0f / 60.0f);
	}
	EXPECT_EQ(world.scene.Get(boulder).changes, 0);
	EXPECT_FALSE(world.scene.Get(boulder).hidden);
	EXPECT_FLOAT_EQ(world.scene.Get(boulder).opacity, 1.0f);
	// The building near the camera still fades.
	EXPECT_FLOAT_EQ(world.scene.Get(world.building).opacity, 0.3f);
	EXPECT_EQ(fade.Faded(), 1);
}

TEST(OccluderFade, GivesBackAFadedObjectInTheFrameThatTheHandTakesIt)
{
	const FadeSettings settings {.enabled = true, .opacity = 0.3f, .seconds = 0.25f, .hideInside = true};
	for (const auto rate : k_Rates)
	{
		SCOPED_TRACE(std::to_string(rate) + " fps");
		const auto deltaTime = 1.0f / static_cast<float>(rate);
		World world;
		const auto boulder = world.scene.Add({-1.0f, 109.0f, -9.0f}, {1.0f, 111.0f, -8.0f});
		OccluderFade fade;
		for (int frame = 0; frame < rate; ++frame)
		{
			fade.Step(world.scene, settings, k_Near, deltaTime);
		}
		ASSERT_FLOAT_EQ(world.scene.Get(boulder).opacity, 0.3f);
		// The hand takes the boulder: it is solid at once, with no fade in.
		world.scene.Get(boulder).held = true;
		fade.Step(world.scene, settings, k_Near, deltaTime);
		EXPECT_FLOAT_EQ(world.scene.Get(boulder).opacity, 1.0f);
		EXPECT_FALSE(world.scene.Get(boulder).hidden);
		EXPECT_EQ(world.scene.Get(boulder).restores, 1);
		EXPECT_FALSE(fade.Objects().contains(boulder));
		// It stays solid while the hand holds it, and the building stays faded.
		const auto changes = world.scene.Get(boulder).changes;
		for (int frame = 0; frame < rate; ++frame)
		{
			fade.Step(world.scene, settings, k_Near, deltaTime);
		}
		EXPECT_EQ(world.scene.Get(boulder).changes, changes);
		EXPECT_FLOAT_EQ(world.scene.Get(boulder).opacity, 1.0f);
		EXPECT_FLOAT_EQ(world.scene.Get(world.building).opacity, 0.3f);
	}
}

} // namespace
