/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cmath>

#include <algorithm>
#include <array>
#include <numbers>
#include <optional>
#include <vector>

#include <core/CameraPivot.h>
#include <mod_core/UnityMath.h>

namespace moa_mod::camera
{

/// The distance between the land samples of a land normal, metres. It is the distance between two points of the height
/// grid of the land, and the step of the land normal of the game.
constexpr float k_LandNormalStep = 0.25f;
/// A point of the camera ball that is less than this distance in the land does not push the camera, metres. Thus a
/// camera that the land pushed out does not move again in the next frame.
constexpr float k_LandPushTolerance = 1e-4f;

/// The land normal at a point, from four land heights around the point (the rule of the game). `height(x, z)` gives the
/// exact land height, or nothing on an error. Nothing if a height is not available.
template <typename Height>
[[nodiscard]] std::optional<WorldPoint> LandNormal(Height&& height, float x, float z)
{
	const auto east = height(x + k_LandNormalStep, z);
	const auto west = height(x - k_LandNormalStep, z);
	const auto north = height(x, z + k_LandNormalStep);
	const auto south = height(x, z - k_LandNormalStep);
	if (!east || !west || !north || !south)
	{
		return std::nullopt;
	}
	const auto normal = Normalize(WorldPoint {*west - *east, 2.0f * k_LandNormalStep, *south - *north});
	return Length(normal) > 0.0f ? normal : WorldPoint {0.0f, 1.0f, 0.0f};
}

/// How the spring arm puts its land samples on the line from the pivot to the camera.
struct ArmSampling
{
	float firstStep = 0.5f;  ///< The distance between the samples at the pivot, metres.
	float growth = 0.02f;    ///< The distance between the samples grows by this part of the distance from the pivot.
	int maximumPoints = 256; ///< The largest count of samples.
};

/// The distances from the pivot of the land samples on an arm of `length` metres. The samples are `firstStep` apart
/// near the pivot, and farther apart by `growth` of the distance from the pivot. If that gives more samples than the
/// maximum, each step becomes longer by the same factor. The last sample is at `length`.
[[nodiscard]] inline std::vector<float> ArmSampleDistances(float length, const ArmSampling& sampling)
{
	std::vector<float> distances;
	if (!(length > 0.0f))
	{
		return distances;
	}
	const auto first = std::max(sampling.firstStep, 1e-3f);
	const auto growth = std::max(sampling.growth, 0.0f);
	const auto maximum = std::max(sampling.maximumPoints, 1);
	// With the steps first + growth * d, the distance after k steps is (first / growth) * ((1 + growth)^k - 1). A
	// factor on each step changes the ratio to (1 + factor * growth).
	auto factor = 1.0f;
	if (growth > 0.0f)
	{
		const auto needed = std::log1p(growth * length / first) / std::log1p(growth);
		if (needed > static_cast<float>(maximum))
		{
			factor = std::expm1(std::log1p(growth * length / first) / static_cast<float>(maximum)) / growth;
		}
	}
	else if (length / first > static_cast<float>(maximum))
	{
		factor = length / (first * static_cast<float>(maximum));
	}
	distances.reserve(static_cast<size_t>(maximum));
	for (auto distance = first * factor; distance < length && static_cast<int>(distances.size()) + 1 < maximum;
	     distance += factor * (first + growth * distance))
	{
		distances.push_back(distance);
	}
	distances.push_back(length);
	return distances;
}

/// The directions of the points of the camera ball, in the frame of the camera (x right, y up, z forward): one point
/// down, rings of 4, 6 and 4 points at 45 degrees down, level and 45 degrees up, and one point up. The points
/// below the camera come first, because the land is usually below the camera.
[[nodiscard]] inline const std::array<WorldPoint, 16>& CameraBallDirections()
{
	static const auto directions = [] {
		std::array<WorldPoint, 16> result {};
		size_t index = 0;
		const auto ring = [&](float elevationDegrees, int count) {
			const auto elevation = elevationDegrees * k_DegreesToRadians;
			for (int point = 0; point < count; ++point)
			{
				const auto azimuth = 2.0f * std::numbers::pi_v<float> * static_cast<float>(point) / static_cast<float>(count);
				result[index++] = {std::cos(elevation) * std::sin(azimuth), std::sin(elevation),
				                   std::cos(elevation) * std::cos(azimuth)};
			}
		};
		ring(-90.0f, 1);
		ring(-45.0f, 4);
		ring(0.0f, 6);
		ring(45.0f, 4);
		ring(90.0f, 1);
		return result;
	}();
	return directions;
}

/// The result of the camera ball.
struct CameraBallPush
{
	WorldPoint position; ///< The camera after the pushes.
	int pushes = 0;      ///< The count of the points that pushed the camera.
};

/// Push a camera out of the land with a ball of points around it. Each point of the ball that is in the land pushes
/// the camera along the land normal at that point, by the distance from the point to the land surface (at most
/// `radius`). The points push one after the other, each from the camera that the points before it pushed. The ball
/// does this again, at most `passes` times, until no point is in the land. `height(x, z)` gives the exact land height.
/// Nothing on an error of the land height.
template <typename Height>
[[nodiscard]] std::optional<CameraBallPush> PushOutOfLand(Height&& height, WorldPoint camera, Quaternion rotation, float radius,
                                                          int passes)
{
	CameraBallPush result {.position = camera, .pushes = 0};
	if (!(radius > 0.0f))
	{
		return result;
	}
	for (int pass = 0; pass < passes; ++pass)
	{
		auto pushed = false;
		for (const auto& direction : CameraBallDirections())
		{
			const auto point = result.position + Rotate(rotation, direction) * radius;
			const auto land = height(point.x, point.z);
			if (!land)
			{
				return std::nullopt;
			}
			const auto depth = *land - point.y;
			if (depth <= k_LandPushTolerance)
			{
				continue;
			}
			auto normal = LandNormal(height, point.x, point.z);
			if (!normal)
			{
				return std::nullopt;
			}
			// The distance from the point to the plane of the land along the normal.
			const auto push = std::min(depth * normal->y, radius);
			if (push <= k_LandPushTolerance)
			{
				continue;
			}
			result.position = result.position + *normal * push;
			++result.pushes;
			pushed = true;
		}
		if (!pushed)
		{
			break;
		}
	}
	return result;
}

/// Move an offset to a goal smoothly: out (farther from zero) in `pushSeconds`, back in `returnSeconds`, each with the
/// ease of `SmoothingStep` (95 percent in the time, the same at each frame rate), and never faster than `maximumSpeed`
/// metres for each second. A time of zero moves at once, also faster than the speed.
[[nodiscard]] inline WorldPoint EasePush(WorldPoint push, WorldPoint goal, float deltaTime, float pushSeconds,
                                         float returnSeconds, float maximumSpeed)
{
	const auto out = Length(goal) > Length(push);
	const auto seconds = out ? pushSeconds : returnSeconds;
	if (seconds <= 0.0f)
	{
		return goal;
	}
	auto step = (goal - push) * SmoothingStep(deltaTime, seconds);
	const auto largest = maximumSpeed * deltaTime;
	if (const auto length = Length(step); length > largest)
	{
		step = step * (largest / length);
	}
	return push + step;
}

} // namespace moa_mod::camera
