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

/// A critically damped spring covers 95 percent of a sudden move in the time `k_ChaseRate / rate`.
constexpr float k_ChaseRate = 4.744f;

/// The camera that the user sees follows a target camera with a critically damped spring. The spring keeps the speed of
/// the camera from frame to frame, and it does not go past a target that stops.
///
/// The target moves at a constant speed in each frame, and the spring follows that path with an exact solution. So a
/// target path gives the same camera path at each frame rate.
struct ChaseSpring
{
	WorldPoint lag;      ///< The camera that the user sees, less the target camera, metres.
	WorldPoint velocity; ///< The velocity of the camera that the user sees, metres for each second.

	/// Put the camera on the target, with no speed.
	void Reset()
	{
		lag = {};
		velocity = {};
	}

	/// True when the camera is on the target and does not move.
	[[nodiscard]] bool Still() const { return IsZero(lag) && IsZero(velocity); }

	/// The target moves by `targetMove` in the frame, at a constant speed. Move the camera after it. `seconds` is the
	/// time in which the camera covers 95 percent of a sudden move; zero or less puts the camera on the target.
	/// The camera never moves faster than `maximumSpeed`, metres for each second; zero or less is no limit.
	void Step(WorldPoint targetMove, float deltaTime, float seconds, float maximumSpeed = 0.0f)
	{
		if (deltaTime <= 0.0f)
		{
			// No time: the camera stays where it is.
			lag = lag - targetMove;
			return;
		}
		const auto targetSpeed = targetMove * (1.0f / deltaTime);
		if (seconds <= 0.0f)
		{
			lag = {};
			velocity = targetSpeed;
			return;
		}
		// The lag less the steady lag of a target at this speed is a free critically damped motion.
		const auto rate = static_cast<double>(k_ChaseRate) / static_cast<double>(seconds);
		const auto steady = 2.0 / rate;
		const auto time = static_cast<double>(deltaTime);
		const auto decay = std::exp(-rate * time);
		auto axis = [&](float lagValue, float speed, float target, float& lagOut, float& speedOut) {
			const auto start = static_cast<double>(lagValue) + static_cast<double>(target) * steady;
			const auto slope = static_cast<double>(speed) - static_cast<double>(target) + start * rate;
			const auto free = (start + slope * time) * decay;
			const auto freeSpeed = (slope - (start + slope * time) * rate) * decay;
			lagOut = static_cast<float>(free - static_cast<double>(target) * steady);
			speedOut = static_cast<float>(static_cast<double>(target) + freeSpeed);
		};
		WorldPoint nextLag;
		WorldPoint nextVelocity;
		axis(lag.x, velocity.x, targetSpeed.x, nextLag.x, nextVelocity.x);
		axis(lag.y, velocity.y, targetSpeed.y, nextLag.y, nextVelocity.y);
		axis(lag.z, velocity.z, targetSpeed.z, nextLag.z, nextVelocity.z);
		if (maximumSpeed > 0.0f)
		{
			// The move of the camera in the frame is the move of the target plus the change of the lag.
			const auto move = targetMove + nextLag - lag;
			const auto most = maximumSpeed * deltaTime;
			if (const auto length = Length(move); length > most)
			{
				nextLag = lag + move * (most / length) - targetMove;
			}
			if (const auto speed = Length(nextVelocity); speed > maximumSpeed)
			{
				nextVelocity = nextVelocity * (maximumSpeed / speed);
			}
		}
		lag = nextLag;
		velocity = nextVelocity;
		if (IsZero(targetMove) && Length(lag) < k_SnapMetres && Length(velocity) < k_SnapSpeed)
		{
			Reset();
		}
	}

private:
	/// The spring stops when the lag and the speed are less than these values.
	static constexpr float k_SnapMetres = 1e-4f;
	static constexpr float k_SnapSpeed = 1e-3f;

	[[nodiscard]] static bool IsZero(WorldPoint point) { return point.x == 0.0f && point.y == 0.0f && point.z == 0.0f; }
};

/// A short smoothing of an input that comes in steps, for example the turn of a mouse drag in each frame. The input
/// of a frame comes at a constant rate over the frame, and the output follows it with a time constant. All of the
/// input comes out: the smoothing only delays it. The exact solution gives the same output at each frame rate.
struct InputSmoother
{
	float pending = 0.0f; ///< The input that did not come out yet.

	void Reset() { pending = 0.0f; }

	/// Add the input of a frame and return the part that comes out in the frame. `seconds` is the time constant; zero
	/// or less gives all the input at once.
	[[nodiscard]] float Step(float input, float deltaTime, float seconds)
	{
		if (seconds <= 0.0f || deltaTime <= 0.0f)
		{
			const auto all = pending + input;
			pending = 0.0f;
			return all;
		}
		const auto rate = static_cast<double>(input) / static_cast<double>(deltaTime);
		const auto constant = static_cast<double>(seconds);
		const auto decay = std::exp(-static_cast<double>(deltaTime) / constant);
		auto next = rate * constant + (static_cast<double>(pending) - rate * constant) * decay;
		if (std::abs(next) < k_SnapInput)
		{
			next = 0.0;
		}
		const auto out = static_cast<double>(pending) + static_cast<double>(input) - next;
		pending = static_cast<float>(next);
		return static_cast<float>(out);
	}

private:
	/// A pending input smaller than this comes out at once.
	static constexpr double k_SnapInput = 1e-6;
};

/// The plane on which the land drag holds the gripped land. It goes through the gripped point. It is level, or it
/// rises to the gripped point when that point is higher than the land under the camera, so that a drag on a slope
/// follows the slope.
struct GripPlane
{
	WorldPoint normal {0.0f, 1.0f, 0.0f}; ///< A unit vector up from the plane.
	float offset = 0.0f;                  ///< The value of `Dot(normal, point)` for each point of the plane.
};

/// Return the plane of the land drag for a gripped point, the camera and the land height under the camera.
[[nodiscard]] inline GripPlane GripPlaneThrough(WorldPoint grip, WorldPoint camera, float landUnderCamera)
{
	constexpr float k_SmallestReach = 1e-3f;
	const WorldPoint base {camera.x, std::min(landUnderCamera, grip.y), camera.z};
	const auto toGrip = grip - base;
	const auto across = std::hypot(toGrip.x, toGrip.z);
	if (across < k_SmallestReach)
	{
		return {.normal = {0.0f, 1.0f, 0.0f}, .offset = grip.y};
	}
	// The normal is square to the line from the base to the gripped point, in the vertical plane of that line, and it
	// points up.
	const auto along = toGrip * (1.0f / Length(toGrip));
	const auto level = across / Length(toGrip);
	const WorldPoint normal {-along.x * along.y / level, level, -along.z * along.y / level};
	return {.normal = normal, .offset = Dot(normal, grip)};
}

/// The limits of a land drag.
struct GripLimits
{
	/// The largest move of the land for each pixel of the cursor, as a part of the distance from the camera to the
	/// focus.
	float stepPerPixel = 0.011f;
	/// The land drag can always move the land this far, metres.
	float minimumStep = 1.0f;
};

/// Return the move of the land under the cursor in a land drag: the point under the cursor now less the point under
/// the cursor at the grip, both on the plane of the drag and both from the camera at the grip (`origin`). The camera
/// moves by the opposite of this move to keep the gripped land under the cursor. The move is never longer than the
/// limit for the move of the cursor from the grip (`pixels`) at the distance from the camera to the focus
/// (`distance`). Nothing if a ray does not go down through the plane.
[[nodiscard]] inline std::optional<WorldPoint> GripLandMove(const GripPlane& plane, WorldPoint origin, WorldPoint rayNow,
                                                            WorldPoint rayAtGrip, float pixels, float distance,
                                                            const GripLimits& limits)
{
	constexpr float k_SmallestDescent = 1e-3f;
	constexpr float k_SmallestReach = 1e-3f;
	const auto descentNow = Dot(plane.normal, rayNow);
	const auto descentAtGrip = Dot(plane.normal, rayAtGrip);
	if (descentNow > -k_SmallestDescent || descentAtGrip > -k_SmallestDescent)
	{
		return std::nullopt;
	}
	const auto toPlane = plane.offset - Dot(plane.normal, origin);
	const auto reachNow = toPlane / descentNow;
	const auto reachAtGrip = toPlane / descentAtGrip;
	if (reachNow < k_SmallestReach || reachAtGrip < k_SmallestReach)
	{
		return std::nullopt;
	}
	auto move = (origin + rayNow * reachNow) - (origin + rayAtGrip * reachAtGrip);
	const auto most = std::max(limits.minimumStep, limits.stepPerPixel * distance * pixels);
	if (const auto length = Length(move); length > most)
	{
		move = move * (most / length);
	}
	return move;
}

/// Return the end of a move of the camera from `from` to `to` that stops `stopDistance` metres before the first point
/// where the line goes under the land. `landAt` gives the land height at a point, or nothing on an error. Nothing on
/// an error. A camera that starts under the land moves with no stop.
template <typename LandAt>
[[nodiscard]] std::optional<WorldPoint> StopBeforeLand(WorldPoint from, WorldPoint to, float stopDistance, LandAt&& landAt)
{
	constexpr float k_SampleMetres = 2.0f;
	constexpr int k_MostSamples = 32;
	const auto move = to - from;
	const auto length = Length(move);
	if (length <= 0.0f)
	{
		return to;
	}
	const auto start = landAt(from);
	if (!start)
	{
		return std::nullopt;
	}
	if (from.y < *start)
	{
		return to;
	}
	const auto samples = std::clamp(static_cast<int>(std::ceil(length / k_SampleMetres)), 1, k_MostSamples);
	for (int index = 1; index <= samples; ++index)
	{
		const auto part = static_cast<float>(index) / static_cast<float>(samples);
		const auto point = from + move * part;
		const auto land = landAt(point);
		if (!land)
		{
			return std::nullopt;
		}
		if (point.y < *land)
		{
			const auto clear = static_cast<float>(index - 1) / static_cast<float>(samples);
			return from + move * std::max(clear - stopDistance / length, 0.0f);
		}
	}
	return to;
}

} // namespace moa_mod::camera
