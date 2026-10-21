/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cmath>

#include <algorithm>
#include <optional>

#include <moa_mod/camera/CameraPitch.h>

namespace moa_mod::camera
{

// The ground look. Below the closest zoom stage of the game (stage 0, Detail), the zoom of the hand rig continues into
// the ground range, from stage 0 to stage -1. In this range the camera goes down to the eye height above the land, and
// the tilt of the stage goes to a level view. At stage -1 the camera stands on the land and turns about itself.

/// The count of zoom stages in the ground range, below stage 0.
constexpr float k_GroundStages = 1.0f;

/// Return the part of the ground range for a zoom stage: 0 at stage 0 and above, 1 at the bottom of the range.
[[nodiscard]] inline float GroundPart(float stage)
{
	return std::clamp(-stage / k_GroundStages, 0.0f, 1.0f);
}

/// Return the blend from the hand rig (0) to the camera that stands on the land (1) for a part of the ground range.
/// The blend has no speed at its two ends, so the tilt and the pivot of the turn start and stop changing smoothly.
[[nodiscard]] inline float GroundBlend(float part)
{
	const auto x = std::clamp(part, 0.0f, 1.0f);
	return x * x * (3.0f - 2.0f * x);
}

/// Return the tilt of the zoom stage in the ground range, degrees: the tilt of the closest stage at the top, and a
/// level view at the bottom.
[[nodiscard]] inline float GroundTilt(float detailTilt, float part)
{
	return detailTilt * (1.0f - GroundBlend(part));
}

/// The limits of the view of a camera that stands on the land, degrees.
struct GroundViewLimits
{
	float down = 60.0f; ///< The largest angle of the view centre below the horizon.
	float up = 60.0f;   ///< The largest angle of the view centre above the horizon.
};

/// Return the range of the raise in the ground range. At the top of the range it is the range of the closest stage
/// (`detail`, with the tilt `detailTilt`). At the bottom, the view centre can go from `limits.down` below the horizon to
/// `limits.up` above it. Between them, the limits of the view tilt change linearly with the blend, so the range has no
/// step at the closest stage.
[[nodiscard]] inline PitchRange GroundPitchRange(const PitchRange& detail, float detailTilt, float part,
                                                 const GroundViewLimits& limits)
{
	const auto blend = GroundBlend(part);
	const auto tilt = GroundTilt(detailTilt, part);
	auto mix = [blend](float top, float bottom) { return top + (bottom - top) * blend; };
	const auto steepestTilt = mix(detailTilt - detail.steepest, limits.down);
	const auto flattestTilt = mix(detailTilt - detail.flattest, -limits.up);
	const auto highestTilt = mix(detailTilt - detail.highest, -limits.up);
	const auto highest = std::max(tilt - highestTilt, 0.0f);
	return {.steepest = std::min(tilt - steepestTilt, 0.0f),
	        .flattest = std::clamp(tilt - flattestTilt, 0.0f, highest),
	        .highest = highest};
}

/// Return the height of the camera above the land for a part of the ground range. The height goes from `topHeight`
/// at the top of the range to `eyeHeight` at the bottom, linearly in the logarithm of the height. Thus a notch of the
/// wheel changes the height by the same ratio at each height.
[[nodiscard]] inline float GroundHeight(float topHeight, float eyeHeight, float part)
{
	const auto top = std::max(topHeight, 1e-3f);
	const auto eye = std::max(eyeHeight, 1e-3f);
	const auto x = std::clamp(part, 0.0f, 1.0f);
	return std::exp(std::log(top) * (1.0f - x) + std::log(eye) * x);
}

/// Return the height at the top of the ground range that gives `height` at `part`: the reverse of `GroundHeight`.
/// Return nothing at the bottom of the range, where the height is always the eye height.
[[nodiscard]] inline std::optional<float> GroundTopHeight(float height, float eyeHeight, float part)
{
	const auto x = std::clamp(part, 0.0f, 1.0f);
	if (x >= 1.0f - 1e-4f)
	{
		return std::nullopt;
	}
	const auto eye = std::max(eyeHeight, 1e-3f);
	return std::exp((std::log(std::max(height, 1e-3f)) - std::log(eye) * x) / (1.0f - x));
}

/// Move the smoothed height of the land under the camera to the land height of this frame. A higher land takes
/// effect at once, so the camera never goes below the land. A lower land takes effect in approximately
/// `fallSeconds`, at each frame rate.
[[nodiscard]] inline float FollowLand(float smoothed, float land, float deltaTime, float fallSeconds)
{
	if (land >= smoothed)
	{
		return land;
	}
	return std::max(FollowValue(smoothed, land, deltaTime, fallSeconds), land);
}

/// Return the speed of the key pan in the ground range, metres for each second: the speed of the hand rig at the top
/// of the range, and the walk speed at the bottom, linearly in the logarithm of the speed.
[[nodiscard]] inline float GroundPanSpeed(float topSpeed, float walkSpeed, float part)
{
	const auto blend = GroundBlend(part);
	const auto top = std::max(topSpeed, 1e-3f);
	const auto walk = std::max(walkSpeed, 1e-3f);
	return std::exp(std::log(top) * (1.0f - blend) + std::log(walk) * blend);
}

} // namespace moa_mod::camera
