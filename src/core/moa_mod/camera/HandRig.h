/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cmath>

#include <algorithm>
#include <span>

#include <moa_mod/UnityMath.h>
#include <moa_mod/camera/CameraPitch.h>

namespace moa_mod::camera
{

/// One row of the zoom stage table of the game: the height of the camera above the focus and the tilt, degrees.
struct StageView
{
	float height = 0.0f;
	float tilt = 0.0f;
};

/// Return the height and the tilt of a zoom stage with the rule of the game: a linear change between the row of the
/// whole stage and the next row. A stage out of the table gets the first or the last row. An empty table gives zero.
[[nodiscard]] inline StageView InterpolateStage(std::span<const StageView> table, float stage)
{
	if (table.empty())
	{
		return {};
	}
	const auto last = static_cast<float>(table.size() - 1);
	const auto clamped = std::clamp(stage, 0.0f, last);
	const auto index = std::min(static_cast<size_t>(clamped), table.size() - 1);
	const auto next = std::min(index + 1, table.size() - 1);
	const auto part = clamped - static_cast<float>(index);
	return {table[index].height + (table[next].height - table[index].height) * part,
	        table[index].tilt + (table[next].tilt - table[index].tilt) * part};
}

/// The camera of the user in the hand rig: a position, a heading and a pitch. The rig has no roll.
struct HandRigPose
{
	WorldPoint position;
	float yaw = 0.0f;   ///< The heading, degrees, with the rule of the game.
	float pitch = 0.0f; ///< The angle of the view centre below the horizon, degrees. Less than zero looks up.
};

/// Return the rotation of the rig camera, as Unity's `Quaternion.Euler(pitch, yaw, 0)`.
[[nodiscard]] inline Quaternion HandRigRotation(float yaw, float pitch)
{
	return Multiply(YawRotation(yaw), PitchRotation(pitch));
}

/// Return the pitch of a camera rotation with no roll: the angle of the view centre below the horizon, degrees.
[[nodiscard]] inline float PitchOf(Quaternion rotation)
{
	const auto forward = Rotate(rotation, WorldPoint {0.0f, 0.0f, 1.0f});
	return -std::asin(std::clamp(forward.y, -1.0f, 1.0f)) / k_DegreesToRadians;
}

/// Turn a camera position about the vertical axis through `pivot`. The camera rotation turns by the same angle
/// about the y axis, so the pivot stays at the same place on the screen.
[[nodiscard]] inline WorldPoint YawAboutPivot(WorldPoint camera, WorldPoint pivot, float degrees)
{
	return TurnAboutPivot(camera, pivot, degrees);
}

/// Turn a camera position about the horizontal axis through `pivot` that is parallel to the right side of the camera.
/// The pitch of the camera changes by the same angle, so the pivot stays at the same place on the screen. A positive
/// angle makes the view steeper and moves the camera up.
[[nodiscard]] inline WorldPoint PitchAboutPivot(WorldPoint camera, WorldPoint pivot, float yaw, float degrees)
{
	const auto heading = YawRotation(yaw);
	const auto turn = Multiply(Multiply(heading, PitchRotation(degrees)), Inverse(heading));
	return pivot + Rotate(turn, camera - pivot);
}

/// Move a camera position on the line through `pivot`. The distance to the pivot changes by `factor`, and the camera
/// rotation does not change, so the pivot stays at the same place on the screen.
[[nodiscard]] inline WorldPoint ZoomAboutPivot(WorldPoint camera, WorldPoint pivot, float factor)
{
	return pivot + (camera - pivot) * factor;
}

/// The town camera state of the game that places the camera of the game at the rig camera.
struct GameCameraState
{
	WorldPoint focus;
	float height = 0.0f; ///< The height of the camera above the focus.
	float tilt = 0.0f;   ///< The tilt that the game uses to place the camera, degrees.
};

/// The smallest height of the camera above the focus that the rig gives to the game, metres.
constexpr float k_MinimumRigHeight = 0.5f;
/// The steepest tilt that the rig gives to the game, degrees. The game divides by the tangent of the tilt.
constexpr float k_SteepestGameTilt = 89.5f;

/// Return the game state that puts the camera of the game at `camera`. The focus is on the view centre at the height
/// `level`, so the rotation of the game (the camera looks at the focus) is the rotation of the rig. When the view
/// centre does not meet that height within `reach` metres horizontally (a flat view or a view up), the focus is at
/// the reach and the camera of the game looks down more steeply than the rig. Its position is always the rig position.
[[nodiscard]] inline GameCameraState GameStateForCamera(WorldPoint camera, float yaw, float pitch, float level, float reach)
{
	const auto height = std::max(camera.y - level, k_MinimumRigHeight);
	const auto floor = std::min(std::atan2(height, std::max(reach, 1.0f)) / k_DegreesToRadians, k_SteepestGameTilt);
	const auto tilt = std::clamp(pitch, floor, k_SteepestGameTilt);
	return {.focus = camera - GameCameraOffset(yaw, height, tilt), .height = height, .tilt = tilt};
}

} // namespace moa_mod::camera
