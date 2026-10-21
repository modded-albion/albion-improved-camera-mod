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

/// The horizontal speed of a point over the last part of a second. The filter gives each frame a weight for its
/// time, and the weight decreases exponentially with the age of the frame. Thus one bad frame does not control the
/// result, and the result does not depend on the frame rate. The first frames get the full weight, so a short drag
/// gives its real speed.
struct VelocityFilter
{
	WorldPoint velocity;            ///< Metres per second. The height is always zero.
	float weight = 0.0f;            ///< The sum of the weights of the frames, seconds.
	std::optional<WorldPoint> last; ///< The point of the last frame.

	/// Forget all frames.
	void Reset()
	{
		velocity = {};
		weight = 0.0f;
		last.reset();
	}

	/// Add the point of this frame. `seconds` is the age at which the weight of a frame is approximately 37 percent.
	void Add(WorldPoint point, float deltaTime, float seconds)
	{
		if (last && deltaTime > 0.0f)
		{
			const auto move = point - *last;
			const WorldPoint speed {move.x / deltaTime, 0.0f, move.z / deltaTime};
			const auto keep = seconds > 0.0f ? weight * std::exp(-deltaTime / seconds) : 0.0f;
			velocity = (velocity * keep + speed * deltaTime) * (1.0f / (keep + deltaTime));
			weight = keep + deltaTime;
		}
		last = point;
	}

	/// Return true when the filter has a speed from at least one move.
	[[nodiscard]] bool HasVelocity() const { return weight > 0.0f; }
};

/// The motion of the focus after the user releases the land. The speed decreases exponentially with the time
/// constant. The move of each frame is the exact integral of the speed over the frame, so the full glide is the
/// same at each frame rate: the start speed times the time constant.
struct Glide
{
	WorldPoint velocity; ///< Metres per second, horizontal.
	bool active = false;

	/// Start a glide. The speed is never more than `maximumSpeed`. Return true if the glide starts: the speed must be
	/// more than `stopSpeed`.
	bool Start(WorldPoint startVelocity, float maximumSpeed, float stopSpeed)
	{
		velocity = {startVelocity.x, 0.0f, startVelocity.z};
		const auto speed = Length(velocity);
		if (speed > maximumSpeed && speed > 0.0f)
		{
			velocity = velocity * (maximumSpeed / speed);
		}
		active = Length(velocity) > stopSpeed;
		if (!active)
		{
			velocity = {};
		}
		return active;
	}

	/// Stop the glide at once.
	void Stop()
	{
		velocity = {};
		active = false;
	}

	/// Return the move of this frame, and decrease the speed. The glide stops after the frame where the speed falls
	/// to `stopSpeed` or less.
	[[nodiscard]] WorldPoint Step(float deltaTime, float timeConstant, float stopSpeed)
	{
		if (!active || deltaTime <= 0.0f || timeConstant <= 0.0f)
		{
			if (timeConstant <= 0.0f)
			{
				Stop();
			}
			return {};
		}
		const auto decay = std::exp(-deltaTime / timeConstant);
		const auto move = velocity * (timeConstant * (1.0f - decay));
		velocity = velocity * decay;
		if (Length(velocity) <= stopSpeed)
		{
			Stop();
		}
		return move;
	}
};

/// The part of the distance to the cursor that the land drag of the game moves its smoothed cursor in one frame,
/// while the user holds the button: 1 - 0.9 ^ (60 * deltaTime).
[[nodiscard]] inline float GameDragCursorStep(float deltaTime)
{
	return 1.0f - std::pow(0.9f, 60.0f * std::max(deltaTime, 0.0f));
}

} // namespace moa_mod::camera
