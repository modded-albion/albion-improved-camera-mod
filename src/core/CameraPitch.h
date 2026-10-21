/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cmath>

#include <algorithm>
#include <optional>

#include <core/CameraPivot.h>
#include <mod_core/UnityMath.h>

namespace moa_mod::camera
{

/// The town camera of the game as four numbers. The camera looks at the focus, down by the tilt, from the distance
/// behind it. The heading turns the camera about the vertical axis through the focus.
struct CameraRig
{
	WorldPoint focus;
	float direction = 0.0f; ///< The heading, degrees, with the same rule as `YawRotation`.
	float tilt = 0.0f;      ///< The angle of the view below the horizon, degrees.
	float distance = 0.0f;  ///< The distance from the camera to the focus.
};

/// Return the rotation of the camera of a rig, as Unity's `Quaternion.Euler(tilt, direction, 0)`.
[[nodiscard]] inline Quaternion RigRotation(const CameraRig& rig)
{
	return Multiply(YawRotation(rig.direction), PitchRotation(rig.tilt));
}

/// Return the position of the camera of a rig. For the tilt and the distance of the game, this is the position that
/// the game calculates: the focus plus the turned offset (0, height, -height / tan(tilt)).
[[nodiscard]] inline WorldPoint RigPosition(const CameraRig& rig)
{
	return rig.focus - Rotate(RigRotation(rig), WorldPoint {0.0f, 0.0f, rig.distance});
}

/// Return the distance from the camera to the focus for the height and the tilt that the game keeps for a zoom
/// stage. Return 0 if the tilt is too small.
[[nodiscard]] inline float StageDistance(float height, float tiltDegrees)
{
	const auto sine = std::sin(tiltDegrees * k_DegreesToRadians);
	return sine > 1e-3f ? height / sine : 0.0f;
}

/// Turn the full camera of a rig to a new tilt, about the horizontal axis through `pivot` that is parallel to the
/// right side of the camera. The pivot stays at the same place on the screen. The focus stays at its height and
/// moves only along the heading, so the distance to the focus changes. Return nothing if the new view does not
/// meet the height of the focus in front of the camera.
[[nodiscard]] inline std::optional<CameraRig> OrbitRig(const CameraRig& rig, WorldPoint pivot, float tilt)
{
	const auto sine = std::sin(tilt * k_DegreesToRadians);
	if (sine <= 1e-3f)
	{
		return std::nullopt;
	}
	const auto heading = YawRotation(rig.direction);
	const auto turn = Multiply(Multiply(heading, PitchRotation(tilt - rig.tilt)), Inverse(heading));
	const auto position = pivot + Rotate(turn, RigPosition(rig) - pivot);
	const auto distance = (position.y - rig.focus.y) / sine;
	if (distance <= 0.0f)
	{
		return std::nullopt;
	}
	CameraRig result {.focus = rig.focus, .direction = rig.direction, .tilt = tilt, .distance = distance};
	const auto focus = position + Rotate(RigRotation(result), WorldPoint {0.0f, 0.0f, distance});
	result.focus = {focus.x, rig.focus.y, focus.z};
	return result;
}

/// Turn a rig to a new tilt. If there is a pivot, the turn is about the pivot as far as the distance to the focus
/// stays between `minimumDistance` and `maximumDistance`. The remaining part of the turn is about the focus, so the
/// rig always gets the new tilt.
[[nodiscard]] inline CameraRig PitchRig(const CameraRig& rig, std::optional<WorldPoint> pivot, float tilt,
                                        float minimumDistance, float maximumDistance)
{
	auto aboutPivot = [&](float to) -> std::optional<CameraRig> {
		auto result = OrbitRig(rig, *pivot, to);
		if (!result || result->distance < minimumDistance || result->distance > maximumDistance)
		{
			return std::nullopt;
		}
		return result;
	};
	auto best = rig;
	if (pivot)
	{
		if (auto full = aboutPivot(tilt))
		{
			return *full;
		}
		// Find the largest part of the turn that the pivot permits.
		float valid = 0.0f;
		float invalid = 1.0f;
		for (int step = 0; step < 16; ++step)
		{
			const auto part = (valid + invalid) * 0.5f;
			if (auto result = aboutPivot(rig.tilt + (tilt - rig.tilt) * part))
			{
				valid = part;
				best = *result;
			}
			else
			{
				invalid = part;
			}
		}
	}
	best.tilt = tilt;
	return best;
}

/// How a pitch moves the camera.
enum class PitchMode
{
	Hand,    ///< One camera rig turns, tilts and zooms about the land under the hand. The game state follows the rig.
	InPlace, ///< The camera stays at its position and turns up or down. The focus moves along the heading.
	Orbit,   ///< The camera turns about the hand point and keeps its distance to it.
};

/// Return the name of a pitch mode in the settings file.
[[nodiscard]] constexpr const char* PitchModeName(PitchMode mode)
{
	switch (mode)
	{
	case PitchMode::Hand:
		return "hand";
	case PitchMode::InPlace:
		return "inPlace";
	case PitchMode::Orbit:
		return "orbit";
	}
	return "";
}

/// The smallest permitted value of the largest horizontal distance from the camera to the focus of a view that turns
/// in place, metres. It is the limit of the land drag.
constexpr float k_InPlaceFocusReach = 500.0f;

/// Return the largest horizontal distance from the camera to the focus of a view that turns in place. It is never
/// less than the distance of the camera of the game, so a raise of zero always gives the camera of the game.
[[nodiscard]] inline float InPlaceFocusReach(float height, float gameTilt)
{
	const auto tangent = std::tan(gameTilt * k_DegreesToRadians);
	return tangent > 1e-3f ? std::max(k_InPlaceFocusReach, height / tangent) : k_InPlaceFocusReach;
}

/// A rig for a view that turns in place, and the angle that the camera turns up from the rig.
struct InPlaceView
{
	CameraRig rig;
	float lookUp = 0.0f; ///< Degrees. More than zero when the view centre does not meet the land in the reach.
};

/// Return the rig for a camera at `height` above the focus, with the view centre down by `viewTilt` degrees. The view
/// tilt can be zero or less (the view centre on the horizon or above it). The focus is the point where the view
/// centre meets the height of the focus, but at most `reach` metres away horizontally. If the focus is at the reach,
/// the rig is steeper than the view, and the camera turns up from the rig by the difference.
[[nodiscard]] inline InPlaceView InPlaceRig(WorldPoint focus, float direction, float height, float viewTilt, float reach)
{
	const auto floor = std::atan2(std::max(height, 1e-3f), reach) / k_DegreesToRadians;
	const auto tilt = std::max(viewTilt, floor);
	return {.rig = {.focus = focus,
	                .direction = direction,
	                .tilt = tilt,
	                .distance = std::max(height, 1e-3f) / std::sin(tilt * k_DegreesToRadians)},
	        .lookUp = tilt - viewTilt};
}

/// Turn a view in place from one view tilt to another. The camera stays at its position, at `height` above the
/// focus. The focus moves only along the heading, so that it stays where the view centre meets the land, in the
/// reach.
[[nodiscard]] inline InPlaceView TurnInPlace(WorldPoint focus, float direction, float height, float fromViewTilt,
                                             float toViewTilt, float reach)
{
	const auto camera = RigPosition(InPlaceRig(focus, direction, height, fromViewTilt, reach).rig);
	auto result = InPlaceRig(focus, direction, height, toViewTilt, reach);
	const auto shift = camera - RigPosition(result.rig);
	result.rig.focus = result.rig.focus + WorldPoint {shift.x, 0.0f, shift.z};
	return result;
}

/// Return the offset from the focus to the camera that the game makes for a heading, a height and a tilt:
/// `Euler(0, direction, 0) * (0, height, -height / tan(tilt))`. Return a zero offset if the tilt is too small.
[[nodiscard]] inline WorldPoint GameCameraOffset(float direction, float height, float tilt)
{
	const auto distance = StageDistance(height, tilt);
	if (distance <= 0.0f)
	{
		return {};
	}
	return RigPosition({.focus = {}, .direction = direction, .tilt = tilt, .distance = distance});
}

/// Return the land point at the view centre of a camera that turned in place: the point where the view centre meets
/// the height `level`, but at most `reach` metres away horizontally. This is the reverse of `InPlaceRig`: the rig of
/// the result at the same view tilt has its camera at `camera`.
[[nodiscard]] inline WorldPoint InPlaceViewFocus(WorldPoint camera, float level, float direction, float viewTilt, float reach)
{
	const auto height = std::max(camera.y - level, 1e-3f);
	const auto floor = std::atan2(height, reach) / k_DegreesToRadians;
	const CameraRig rig {.focus = {}, .direction = direction, .tilt = std::max(viewTilt, floor), .distance = 0.0f};
	const auto centre =
	    camera + Rotate(RigRotation(rig), WorldPoint {0.0f, 0.0f, height / std::sin(rig.tilt * k_DegreesToRadians)});
	return {centre.x, level, centre.z};
}

/// The limits of the pitch control. One value, the raise, controls the view: a positive raise makes the view
/// flatter. When the tilt is at its flattest, more raise turns the view up about the camera (the look up).
struct PitchLimits
{
	bool pitch = true;         ///< The raise changes the tilt. If false, the tilt is always the tilt of the game.
	float minimumTilt = 5.0f;  ///< The flattest tilt, degrees below the horizon.
	float maximumTilt = 85.0f; ///< The steepest tilt, degrees below the horizon.
	bool lookUp = false;       ///< Raise above the flattest tilt turns the view up near the ground.
	LookUpLimits lookUpLimits; ///< The limits of the look up.
};

/// The range of the raise at one zoom stage, degrees. Between `steepest` and `flattest`, the raise changes the
/// tilt. Between `flattest` and `highest`, it is the look up.
struct PitchRange
{
	float steepest = 0.0f; ///< Zero or less.
	float flattest = 0.0f; ///< Zero or more.
	float highest = 0.0f;  ///< `flattest` or more.
};

/// Return the range of the raise for a zoom stage and the tilt of the game at that stage. The tilt of the game is
/// always in the range, so a raise of zero always gives the camera of the game.
[[nodiscard]] inline PitchRange PitchRangeFor(float stage, float gameTilt, const PitchLimits& limits)
{
	const auto flattestTilt = limits.pitch ? std::min(limits.minimumTilt, gameTilt) : gameTilt;
	const auto steepestTilt = limits.pitch ? std::max(limits.maximumTilt, gameTilt) : gameTilt;
	const auto lookUp = limits.lookUp ? LookUpFade(stage, limits.lookUpLimits) *
	                                        std::max(flattestTilt + limits.lookUpLimits.maxAboveHorizon, 0.0f)
	                                  : 0.0f;
	const auto flattest = gameTilt - flattestTilt;
	return {.steepest = gameTilt - steepestTilt, .flattest = flattest, .highest = flattest + lookUp};
}

/// The view that a raise gives: the tilt of the camera and the look up about the camera.
struct PitchView
{
	float tilt = 0.0f;
	float lookUp = 0.0f;
};

/// Divide a raise into the tilt and the look up. The two parts join with no step: the look up starts at zero where
/// the tilt gets to its flattest value.
[[nodiscard]] inline PitchView SplitPitch(float raise, float gameTilt, const PitchRange& range)
{
	return {.tilt = gameTilt - std::clamp(raise, range.steepest, range.flattest),
	        .lookUp = std::clamp(raise - range.flattest, 0.0f, range.highest - range.flattest)};
}

/// Change the target raise by the input of this frame (degrees, up is positive). The input cannot push the target
/// out of the range. If the target is out of the range (because the user zoomed), it moves back into the range in
/// approximately `easeSeconds`, at each frame rate.
[[nodiscard]] inline float StepPitchTarget(float target, float input, const PitchRange& range, float deltaTime,
                                           float easeSeconds)
{
	if (input > 0.0f)
	{
		target = std::min(target + input, std::max(range.highest, target));
	}
	else if (input < 0.0f)
	{
		target = std::max(target + input, std::min(range.steepest, target));
	}
	const auto ease = SmoothingStep(deltaTime, easeSeconds);
	if (target > range.highest)
	{
		target += (range.highest - target) * ease;
	}
	else if (target < range.steepest)
	{
		target += (range.steepest - target) * ease;
	}
	return target;
}

/// Move a value to a goal by `SmoothingStep`, so that the result does not depend on the frame rate.
[[nodiscard]] inline float FollowValue(float value, float goal, float deltaTime, float seconds)
{
	return value + (goal - value) * SmoothingStep(deltaTime, seconds);
}

/// The times of the raise that keeps the camera above the land.
struct ClearanceTimes
{
	float riseSeconds = 0.1f; ///< The time to rise to a higher land.
	float fallSeconds = 0.5f; ///< The time to go down when the land is lower again.
};

/// Return the raise of the camera that keeps it above the land. `needed` is the raise that puts the camera at the
/// clearance above the land height that the game gives. The raise follows `needed` smoothly. But the raise is
/// always `needed` less `clearance` or more, so the camera never goes below that land height. The raise is never
/// below zero.
[[nodiscard]] inline float StepClearance(float raise, float needed, float clearance, float deltaTime,
                                         const ClearanceTimes& times = {})
{
	needed = std::max(needed, 0.0f);
	raise = FollowValue(raise, needed, deltaTime, needed > raise ? times.riseSeconds : times.fallSeconds);
	return std::max({raise, needed - clearance, 0.0f});
}

} // namespace moa_mod::camera
