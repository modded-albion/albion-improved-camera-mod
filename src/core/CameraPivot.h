/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cmath>

#include <algorithm>
#include <optional>

#include <mod_core/UnityMath.h>

namespace moa_mod::camera
{

/// Turn a point about the vertical axis through `pivot`, by the same rule as a Unity rotation of `degrees` about
/// the y axis (clockwise when seen from above). The height of the point does not change.
[[nodiscard]] inline WorldPoint TurnAboutPivot(WorldPoint point, WorldPoint pivot, float degrees)
{
	const auto radians = degrees * k_DegreesToRadians;
	const auto cosine = std::cos(radians);
	const auto sine = std::sin(radians);
	const auto dx = point.x - pivot.x;
	const auto dz = point.z - pivot.z;
	return {.x = pivot.x + dx * cosine + dz * sine, .y = point.y, .z = pivot.z - dx * sine + dz * cosine};
}

/// Return the new focus of the game camera when the heading changed by `headingChange` degrees in this frame and
/// the camera must turn about the hand, not about the focus. The camera keeps its offset from the focus, so the
/// hand stays at the same place on the screen.
[[nodiscard]] inline WorldPoint FocusForHandPivot(WorldPoint focus, WorldPoint hand, float headingChange)
{
	return TurnAboutPivot(focus, hand, headingChange);
}

/// Return the part of the distance to a target that a smoothed value moves in one frame, for a time `seconds` that
/// takes the value approximately 95 percent of the way. The result does not depend on the frame rate.
[[nodiscard]] inline float SmoothingStep(float deltaTime, float seconds)
{
	if (seconds <= 0.0f)
	{
		return 1.0f;
	}
	// 95 percent in `seconds` means exp(-k * seconds) = 0.05, so k = ln(20) / seconds.
	return 1.0f - std::exp(-std::log(20.0f) / seconds * deltaTime);
}

/// The town camera of the game at the end of a frame: the land point that it looks at, the camera transform, and
/// the heading of the camera in degrees.
struct CameraPose
{
	WorldPoint focus;
	WorldPoint position;
	Quaternion rotation;
	float direction = 0.0f;
};

/// Turn the full camera (focus, position and rotation) about the vertical axis through `pivot`. The heading
/// changes by the same angle. The game puts the camera at the focus plus an offset that the heading turns with the
/// same rule, so the result is the pose that the game makes for the new heading.
[[nodiscard]] inline CameraPose TurnCamera(const CameraPose& pose, WorldPoint pivot, float degrees)
{
	return {
	    .focus = TurnAboutPivot(pose.focus, pivot, degrees),
	    .position = TurnAboutPivot(pose.position, pivot, degrees),
	    .rotation = Multiply(YawRotation(degrees), pose.rotation),
	    .direction = WrapDegrees(pose.direction + degrees),
	};
}

/// How the camera mod changes the turn of one frame.
struct TurnChange
{
	float startDirection = 0.0f;     ///< The heading before the game frame.
	float direction = 0.0f;          ///< The heading that the mod wants at the end of the frame.
	std::optional<WorldPoint> pivot; ///< The hand point, if the turn must be about the hand.
	bool invertPivot = false;        ///< Turn about the hand in the opposite direction (the sign setting).
};

/// Change the pose that the game made in this frame. First, the camera turns about the focus to the heading that
/// the mod wants. Then, if there is a pivot, the turn of this frame moves from the focus to the pivot. That second
/// step only moves the camera and the focus sideways: the rotation does not change.
[[nodiscard]] inline CameraPose ChangeTurn(const CameraPose& game, const TurnChange& change)
{
	auto pose = TurnCamera(game, game.focus, WrapDegrees(change.direction - game.direction));
	if (change.pivot)
	{
		const auto turn = WrapDegrees(change.direction - change.startDirection) * (change.invertPivot ? -1.0f : 1.0f);
		const auto shift = FocusForHandPivot(pose.focus, *change.pivot, turn) - pose.focus;
		pose.focus = pose.focus + WorldPoint {shift.x, 0.0f, shift.z};
		pose.position = pose.position + WorldPoint {shift.x, 0.0f, shift.z};
	}
	return pose;
}

/// A heading that follows the turns of the game with a delay. The target collects each turn that the game made;
/// the value moves to the target by `SmoothingStep`.
struct HeadingSmoother
{
	float target = 0.0f;
	float value = 0.0f;

	/// Start again from the heading of the game, with no turn that waits.
	void Reset(float direction)
	{
		target = direction;
		value = direction;
	}

	/// Add the turn of this frame (from `startDirection` to `gameDirection`) and return the smoothed heading.
	[[nodiscard]] float Step(float startDirection, float gameDirection, float deltaTime, float seconds)
	{
		target += WrapDegrees(gameDirection - startDirection);
		value += (target - value) * SmoothingStep(deltaTime, seconds);
		// Keep both numbers small. A change of the same full turn to both does not change the heading.
		const auto turns = std::round(value / 360.0f) * 360.0f;
		target -= turns;
		value -= turns;
		return WrapDegrees(value);
	}
};

/// The zoom stages that the game permits.
struct StageLimits
{
	float minimum = 0.0f;
	float maximum = 4.0f;
};

/// Return the limits that the game uses for the zoom stage: the stage count less one, or the zoom limit of a
/// clamp region when it is zero or more, and the minimum zoom level. The minimum is never above the maximum.
[[nodiscard]] inline StageLimits StageLimitsFor(int stageCount, float minimumZoom, float clampMaxZoom)
{
	const auto maximum = clampMaxZoom >= 0.0f ? clampMaxZoom : static_cast<float>(std::max(stageCount - 1, 0));
	return {.minimum = std::min(minimumZoom, maximum), .maximum = maximum};
}

/// The value of the Unity mouse wheel axis for one notch of the wheel, with the default settings of the axis.
constexpr float k_WheelAxisPerNotch = 0.1f;

/// Return the change of the zoom stage for a value of the mouse wheel axis. A turn of the wheel away from the user
/// gives a positive value and zooms in, which makes the stage smaller.
[[nodiscard]] inline float WheelStageChange(float wheelAxis, float stagesPerNotch)
{
	return -wheelAxis / k_WheelAxisPerNotch * stagesPerNotch;
}

/// A zoom stage that moves smoothly to a target. The mouse wheel changes the target in the limits of the game.
struct ZoomSmoother
{
	float target = 0.0f;
	float value = 0.0f;

	/// Start again from the stage of the game.
	void Reset(float stage)
	{
		target = stage;
		value = stage;
	}

	/// Add a change to the target and keep the target in the limits.
	void AddToTarget(float change, StageLimits limits) { target = std::clamp(target + change, limits.minimum, limits.maximum); }

	/// Move the value to the target and return it. The value also stays in the limits.
	[[nodiscard]] float Step(float deltaTime, float seconds, StageLimits limits)
	{
		target = std::clamp(target, limits.minimum, limits.maximum);
		value += (target - value) * SmoothingStep(deltaTime, seconds);
		value = std::clamp(value, limits.minimum, limits.maximum);
		return value;
	}

	/// Move the value to the target and return it. The target stays in the limits. The value moves into the limits
	/// with the same smoothing as a change of the target, so a change of the limits does not make a jump.
	[[nodiscard]] float StepIntoLimits(float deltaTime, float seconds, StageLimits limits)
	{
		target = std::clamp(target, limits.minimum, limits.maximum);
		value += (target - value) * SmoothingStep(deltaTime, seconds);
		return value;
	}
};

/// The highest angle of the view centre above the horizon that the mod permits, degrees. The view stops just before
/// straight up. Thus the heading of the camera stays defined, and the view does not turn over or spin.
constexpr float k_HighestLookUpDegrees = 89.5f;

/// The limits of the look up. The game always points the town camera at the focus, down by the tilt of the zoom stage.
/// The look up turns the camera up from that direction.
struct LookUpLimits
{
	float maxAboveHorizon = k_HighestLookUpDegrees; ///< The highest angle of the view centre above the horizon, degrees.
	float fullStage = 4.0f;                         ///< At this stage and below, the full look up is permitted.
	float zeroStage = 5.0f;                         ///< At this stage and above, no look up is permitted.
};

/// Return the part of the look up that a stage permits: 1 at the full stage and below, 0 at the zero stage and
/// above, with a smooth change between them.
[[nodiscard]] inline float LookUpFade(float stage, const LookUpLimits& limits)
{
	if (limits.zeroStage <= limits.fullStage)
	{
		return stage <= limits.fullStage ? 1.0f : 0.0f;
	}
	const auto t = std::clamp((stage - limits.fullStage) / (limits.zeroStage - limits.fullStage), 0.0f, 1.0f);
	return 1.0f - t * t * (3.0f - 2.0f * t);
}

/// Return the rotation that turns a camera up by `degrees` about its own x axis. Multiply the camera rotation by it
/// on the right. Unity turns the view down for a positive angle about x, so the angle is negative.
[[nodiscard]] inline Quaternion LookUpRotation(float degrees)
{
	return PitchRotation(-degrees);
}

} // namespace moa_mod::camera
