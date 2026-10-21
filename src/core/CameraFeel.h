/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cmath>

#include <algorithm>
#include <span>

#include <core/CameraPivot.h>
#include <core/HandRig.h>
#include <mod_core/UnityMath.h>

namespace moa_mod::camera
{

// The continuous zoom. The zoom stage of the game is a number from 0 (Detail) to the last row of the stage table
// (Map). The height of a stage is a linear change between two rows, as the game calculates it. The continuous zoom
// changes the height by a fixed ratio for each notch of the wheel, so the zoom speed follows the height.

/// The smallest height of a zoom stage that the zoom uses to divide, metres.
constexpr float k_SmallestZoomHeight = 0.1f;

/// Return true if the zoom continues below stage 0 into the ground range: `groundHeight` is more than 0 and less than
/// the height of the first row of the table.
[[nodiscard]] inline bool HasGroundRange(std::span<const StageView> table, float groundHeight)
{
	return table.size() >= 2 && groundHeight > 0.0f && groundHeight < table.front().height;
}

/// Return the height of a zoom stage, as the game calculates it from the stage table. With a ground range, a stage from
/// 0 to -1 goes from the height of the first row to `groundHeight`, linearly in the logarithm of the height.
[[nodiscard]] inline float HeightOfStage(std::span<const StageView> table, float stage, float groundHeight = 0.0f)
{
	if (stage < 0.0f && HasGroundRange(table, groundHeight))
	{
		const auto part = std::min(-stage, 1.0f);
		return table.front().height * std::pow(groundHeight / table.front().height, part);
	}
	return std::max(InterpolateStage(table, stage).height, k_SmallestZoomHeight);
}

/// Return the zoom stage of a height: the reverse of `HeightOfStage`. The heights of the table must increase from row
/// to row. A height out of the table (and out of the ground range) gives the first or the last stage.
[[nodiscard]] inline float StageOfHeight(std::span<const StageView> table, float height, float groundHeight = 0.0f)
{
	if (table.size() < 2)
	{
		return 0.0f;
	}
	if (height <= table.front().height)
	{
		if (!HasGroundRange(table, groundHeight) || height >= table.front().height)
		{
			return 0.0f;
		}
		const auto part = std::log(height / table.front().height) / std::log(groundHeight / table.front().height);
		return -std::clamp(part, 0.0f, 1.0f);
	}
	for (size_t index = 0; index + 1 < table.size(); ++index)
	{
		const auto low = table[index].height;
		const auto high = table[index + 1].height;
		if (height <= high)
		{
			const auto part = high > low ? (height - low) / (high - low) : 0.0f;
			return static_cast<float>(index) + std::clamp(part, 0.0f, 1.0f);
		}
	}
	return static_cast<float>(table.size() - 1);
}

/// Return the zoom stage after a count of notches of the mouse wheel. Each notch toward the user (a negative count)
/// multiplies the height by `ratio`, and each notch away from the user divides it by `ratio`. The result stays in the
/// limits.
[[nodiscard]] inline float StageAfterNotches(std::span<const StageView> table, float stage, float notches, float ratio,
                                             StageLimits limits, float groundHeight = 0.0f)
{
	const auto height = HeightOfStage(table, stage, groundHeight) * std::pow(std::max(ratio, 1.0f), -notches);
	return std::clamp(StageOfHeight(table, height, groundHeight), limits.minimum, limits.maximum);
}

/// Move a zoom stage to a target in the logarithm of the height. Each frame moves the same part of the remaining ratio
/// of heights, so the zoom speed follows the height and does not depend on the frame rate.
[[nodiscard]] inline float StepStageInHeight(std::span<const StageView> table, float value, float target, float deltaTime,
                                             float seconds, float groundHeight = 0.0f)
{
	const auto step = SmoothingStep(deltaTime, seconds);
	if (step >= 1.0f)
	{
		return target;
	}
	// A stage out of the range of the table has no height of its own. Move it as a stage.
	const auto first = HasGroundRange(table, groundHeight) ? -1.0f : 0.0f;
	const auto last = static_cast<float>(table.size()) - 1.0f;
	if (table.size() < 2 || value < first || target < first || value > last || target > last)
	{
		return value + (target - value) * step;
	}
	const auto from = std::log(HeightOfStage(table, value, groundHeight));
	const auto to = std::log(HeightOfStage(table, target, groundHeight));
	return StageOfHeight(table, std::exp(from + (to - from) * step), groundHeight);
}

// The pan of the keys and the edge scroll.

/// The speed of the key pan of the game, in units of the stick, before the game multiplies it by the height factor
/// and the move speed setting.
constexpr float k_GameKeyPanSpeed = 50.0f;

/// Return the factor by which the game multiplies the key pan for the height of the camera: the height of the camera
/// (world y) divided by 100, from 1 to 10.
[[nodiscard]] inline float GamePanHeightFactor(float cameraY)
{
	return std::clamp(cameraY * 0.01f, 1.0f, 10.0f);
}

/// Return the width of the view, metres, at a distance in front of the camera.
[[nodiscard]] inline float ViewWidth(float distance, float verticalFieldOfView, float aspect)
{
	const auto halfHeight = std::tan(std::clamp(verticalFieldOfView, 1.0f, 179.0f) * 0.5f * k_DegreesToRadians);
	return 2.0f * std::max(distance, 0.0f) * halfHeight * std::max(aspect, 0.01f);
}

/// Return the factor for the move speed setting of the game that gives a key pan of `viewWidthsPerSecond` view widths
/// for each second at the move speed 1. The game multiplies the key pan by the height factor and by the setting.
[[nodiscard]] inline float PanSpeedScale(float viewWidthsPerSecond, float viewWidth, float cameraY)
{
	return viewWidthsPerSecond * viewWidth / (k_GameKeyPanSpeed * GamePanHeightFactor(cameraY));
}

/// Return the direction of the edge scroll of the game for a cursor: x to the right and y forward, each from -1 to 1.
/// A cursor in the outer tenth of the screen scrolls to that side. On one side only, the other axis follows the place of
/// the cursor along that side. A cursor in the middle gives zero.
[[nodiscard]] inline Vector2 EdgeScrollDirection(WorldPoint cursor, Vector2 screen)
{
	if (screen.x <= 0.0f || screen.y <= 0.0f)
	{
		return {};
	}
	const auto across = cursor.x / screen.x;
	const auto up = cursor.y / screen.y;
	auto side = [](float part) { return part < 0.1f ? -1.0f : (part > 0.9f ? 1.0f : 0.0f); };
	auto ramp = [](float part) { return std::clamp((part - 0.5f) / 0.4f, -1.0f, 1.0f); };
	auto x = side(across);
	auto y = side(up);
	if (x == 0.0f && y == 0.0f)
	{
		return {};
	}
	if (x == 0.0f)
	{
		x = ramp(across);
	}
	else if (y == 0.0f)
	{
		y = ramp(up);
	}
	return {.x = x, .y = y};
}

/// Return the move of the focus of the game for a pan in the axes of the camera: x along the right side of the camera
/// and y along its forward direction, both in the horizontal plane.
[[nodiscard]] inline WorldPoint PanMove(Vector2 pan, Quaternion cameraRotation)
{
	const auto right = Rotate(cameraRotation, WorldPoint {1.0f, 0.0f, 0.0f});
	const auto forward = Rotate(cameraRotation, WorldPoint {0.0f, 0.0f, 1.0f});
	return Normalize(WorldPoint {right.x, 0.0f, right.z}) * pan.x + Normalize(WorldPoint {forward.x, 0.0f, forward.z}) * pan.y;
}

/// Return the move of the edge scroll of the game in one frame.
[[nodiscard]] inline WorldPoint EdgeScrollMove(Vector2 direction, float speed, float deltaTime, Quaternion cameraRotation)
{
	return PanMove({.x = direction.x * speed * deltaTime, .y = direction.y * speed * deltaTime}, cameraRotation);
}

// The glide after a turn and a pitch.

/// The rate of change of an angle over the last part of a second. Each frame has a weight for its time, and the weight
/// decreases exponentially with the age of the frame, so the result does not depend on the frame rate.
struct RateFilter
{
	float rate = 0.0f;   ///< Degrees for each second.
	float weight = 0.0f; ///< The sum of the weights of the frames, seconds.

	void Reset()
	{
		rate = 0.0f;
		weight = 0.0f;
	}

	/// Add the change of this frame. `seconds` is the age at which the weight of a frame is approximately 37 percent.
	void Add(float change, float deltaTime, float seconds)
	{
		if (deltaTime <= 0.0f)
		{
			return;
		}
		const auto keep = seconds > 0.0f ? weight * std::exp(-deltaTime / seconds) : 0.0f;
		rate = (rate * keep + change) / (keep + deltaTime);
		weight = keep + deltaTime;
	}
};

/// The continued turn of an angle after the input stops. The rate decreases exponentially with the time constant. The
/// change of each frame is the exact integral of the rate over the frame, so the full glide is the start rate times
/// the time constant at each frame rate.
struct AngleGlide
{
	float rate = 0.0f; ///< Degrees for each second.
	bool active = false;

	/// Start a glide. Return true if the rate is more than `stopRate`.
	bool Start(float startRate, float stopRate)
	{
		rate = startRate;
		active = std::abs(rate) > stopRate;
		if (!active)
		{
			rate = 0.0f;
		}
		return active;
	}

	void Stop()
	{
		rate = 0.0f;
		active = false;
	}

	/// Return the change of this frame, and decrease the rate. The glide stops after the frame where the rate falls to
	/// `stopRate` or less.
	[[nodiscard]] float Step(float deltaTime, float timeConstant, float stopRate)
	{
		if (!active || deltaTime <= 0.0f || timeConstant <= 0.0f)
		{
			if (timeConstant <= 0.0f)
			{
				Stop();
			}
			return 0.0f;
		}
		const auto decay = std::exp(-deltaTime / timeConstant);
		const auto change = rate * timeConstant * (1.0f - decay);
		rate *= decay;
		if (std::abs(rate) <= stopRate)
		{
			Stop();
		}
		return change;
	}
};

} // namespace moa_mod::camera
