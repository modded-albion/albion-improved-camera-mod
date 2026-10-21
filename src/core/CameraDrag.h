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

/// A ray with the layout of a Unity `Ray`: a start point and a direction.
struct Ray
{
	WorldPoint origin;
	WorldPoint direction;
};

/// A perspective camera that shows the world on a screen of `width` by `height` pixels. The screen origin is at the
/// bottom left, as in Unity.
struct ScreenCamera
{
	WorldPoint position;
	Quaternion rotation;
	float verticalFieldOfView = 60.0f; ///< Degrees.
	float width = 1920.0f;
	float height = 1080.0f;
};

/// Return the ray from the camera through a point of the screen, as Unity's `Camera.ScreenPointToRay` for a
/// perspective camera. The ray starts at the camera position.
[[nodiscard]] inline Ray ScreenRay(const ScreenCamera& camera, Vector2 screen)
{
	const auto halfHeight = std::tan(camera.verticalFieldOfView * 0.5f * k_DegreesToRadians);
	const auto halfWidth = halfHeight * camera.width / camera.height;
	const WorldPoint local {(2.0f * screen.x / camera.width - 1.0f) * halfWidth,
	                        (2.0f * screen.y / camera.height - 1.0f) * halfHeight, 1.0f};
	return {.origin = camera.position, .direction = Normalize(Rotate(camera.rotation, local))};
}

/// Return where a point of the world is on the screen, or nothing if the point is not in front of the camera.
[[nodiscard]] inline std::optional<Vector2> ScreenPoint(const ScreenCamera& camera, WorldPoint point)
{
	const auto local = Rotate(Inverse(camera.rotation), point - camera.position);
	if (local.z <= 1e-4f)
	{
		return std::nullopt;
	}
	const auto halfHeight = std::tan(camera.verticalFieldOfView * 0.5f * k_DegreesToRadians);
	const auto halfWidth = halfHeight * camera.width / camera.height;
	return Vector2 {(local.x / local.z / halfWidth + 1.0f) * 0.5f * camera.width,
	                (local.y / local.z / halfHeight + 1.0f) * 0.5f * camera.height};
}

/// Return the ray through the same point of the screen for the same camera at another position and rotation.
[[nodiscard]] inline Ray MoveRay(const Ray& ray, WorldPoint fromPosition, Quaternion fromRotation, WorldPoint toPosition,
                                 Quaternion toRotation)
{
	const auto turn = Multiply(toRotation, Inverse(fromRotation));
	return {.origin = toPosition + Rotate(turn, ray.origin - fromPosition), .direction = Rotate(turn, ray.direction)};
}

/// The limits that keep the land drag finite when the cursor is near or above the horizon.
struct HoldLimits
{
	float maximumDistance = 500.0f; ///< The largest horizontal distance from the camera to the point, metres.
	float minimumHeight = 0.5f;     ///< A camera nearer to the level than this is used at this height above it.
};

/// Return the point where a ray meets the level plane at the height `level`, with the rule of the land drag of the
/// game. The point is never more than the maximum distance from the camera, horizontally. A ray that goes up, or
/// meets the level farther away, gives the point at the maximum distance in the horizontal direction of the ray. If
/// the ray starts less than the minimum height above the level, the function uses the minimum height. So the point
/// is always in front of the camera, and it changes continuously with the ray. Inside the limits, the result is the
/// result of the game.
[[nodiscard]] inline WorldPoint PointOnLevel(const Ray& ray, float level, const HoldLimits& limits = {})
{
	const auto direction = Normalize(ray.direction);
	const auto across = std::hypot(direction.x, direction.z);
	if (across < 1e-6f)
	{
		return {ray.origin.x, level, ray.origin.z};
	}
	const auto above = std::max(ray.origin.y - level, limits.minimumHeight);
	const auto distance =
	    direction.y < 0.0f ? std::min(above * across / -direction.y, limits.maximumDistance) : limits.maximumDistance;
	return {ray.origin.x + direction.x / across * distance, level, ray.origin.z + direction.z / across * distance};
}

/// Return the level move of the camera and the focus that puts `grab` under the point of the screen of `ray`. The
/// ray is the ray of the camera before the move. The move is horizontal, as in the land drag of the game.
[[nodiscard]] inline WorldPoint HoldShift(const Ray& ray, WorldPoint grab, const HoldLimits& limits = {})
{
	const auto point = PointOnLevel(ray, grab.y, limits);
	return {grab.x - point.x, 0.0f, grab.z - point.z};
}

/// What the land drag of the game did in one frame, as the camera mod reads it after the frame.
struct DragFrame
{
	bool holding = false;                 ///< The game held its grab point under the cursor in this frame.
	bool grabbedAgain = false;            ///< A press in this frame made the game take a new grab point.
	WorldPoint gameGrab;                  ///< The grab point of the game after the frame.
	std::optional<WorldPoint> pressPoint; ///< The land under the cursor in the view of the user at the frame start.
};

/// The grab point of the land drag in the view that the user sees. The game keeps a grab point for its own camera.
/// This grab point follows the same rules in the view of the user: a new point at a press, the land under the cursor
/// of the last frame when a hold starts, and the pan of the game while the hold continues.
struct DragGrab
{
	std::optional<WorldPoint> grab;      ///< The grab point while the game holds.
	std::optional<WorldPoint> idlePoint; ///< The land under the cursor in the view of the user, in the last frame.
	WorldPoint gameGrabWritten;          ///< The grab point that the mod gave to the game for its next frame.

	/// Return the grab point for this frame, or nothing if the game does not hold.
	[[nodiscard]] std::optional<WorldPoint> Step(const DragFrame& frame)
	{
		if (!frame.holding)
		{
			grab.reset();
			return std::nullopt;
		}
		if (frame.grabbedAgain)
		{
			// The game takes the land under the cursor at the frame start, or keeps its point if the cursor is not on
			// the land.
			if (frame.pressPoint)
			{
				grab = frame.pressPoint;
			}
			else if (!grab)
			{
				grab = idlePoint ? *idlePoint : frame.gameGrab;
			}
		}
		else if (grab)
		{
			// The game adds its pan of this frame to its grab point. Add the same pan.
			grab = *grab + (frame.gameGrab - gameGrabWritten);
		}
		else
		{
			grab = idlePoint ? *idlePoint : frame.gameGrab;
		}
		return grab;
	}
};

} // namespace moa_mod::camera
