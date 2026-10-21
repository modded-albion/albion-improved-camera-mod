/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cmath>

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <moa/common/Result.h>
#include <moa_mod/UnityMath.h>
#include <moa_mod/camera/CameraFeel.h>
#include <moa_mod/camera/HandRig.h>
#include <nlohmann/json.hpp>

namespace moa_mod::camera
{

// The navigation of the hand rig: the flight to a point, the saved camera spots, the reset of the view and the soft
// edge of the map.

/// The box that the game keeps the focus of its camera in, in the horizontal plane.
struct FocusBox
{
	float minimumX = 0.0f;
	float minimumZ = 0.0f;
	float maximumX = 0.0f;
	float maximumZ = 0.0f;

	friend bool operator==(const FocusBox&, const FocusBox&) = default;
};

/// Return the point with x and z in the box. The height does not change.
[[nodiscard]] inline WorldPoint ClampToBox(WorldPoint point, const FocusBox& box)
{
	return {std::clamp(point.x, box.minimumX, box.maximumX), point.y, std::clamp(point.z, box.minimumZ, box.maximumZ)};
}

/// Return the distance to the edge after a move toward the edge, with a soft edge. `distance` is the distance to the
/// edge before the move (zero or more), and `approach` is the move toward the edge (zero or more). Out of the margin
/// the move is the full move. In the margin, the distance falls exponentially with the move: each metre of move keeps
/// `exp(-1 / margin)` of the distance. So the speed falls to zero at the edge with no step, the result never passes
/// the edge, and two moves give the same result as one move of the same length (frame-rate independence).
[[nodiscard]] inline float SoftEdgeDistance(float distance, float approach, float margin)
{
	distance = std::max(distance, 0.0f);
	if (approach <= 0.0f)
	{
		return distance - approach;
	}
	if (margin <= 0.0f)
	{
		return std::max(distance - approach, 0.0f);
	}
	if (distance - approach >= margin)
	{
		return distance - approach;
	}
	// The linear part goes to the margin, and the rest is in the margin.
	const auto linear = std::max(distance - margin, 0.0f);
	const auto start = std::min(distance, margin);
	return start * std::exp(-(approach - linear) / margin);
}

/// Return the move of one axis with the soft edge on both sides of the range `[minimum, maximum]`. A value out of
/// the range moves only toward the range.
[[nodiscard]] inline float SoftEdgeAxis(float value, float move, float minimum, float maximum, float margin)
{
	if (move > 0.0f)
	{
		if (value >= maximum)
		{
			return 0.0f;
		}
		return maximum - SoftEdgeDistance(maximum - value, move, margin) - value;
	}
	if (move < 0.0f)
	{
		if (value <= minimum)
		{
			return 0.0f;
		}
		return minimum + SoftEdgeDistance(value - minimum, -move, margin) - value;
	}
	return 0.0f;
}

/// Return the horizontal move of a point with the soft edge of the box. The height of the move does not change.
[[nodiscard]] inline WorldPoint SoftEdgeMove(WorldPoint point, WorldPoint move, const FocusBox& box, float margin)
{
	return {SoftEdgeAxis(point.x, move.x, box.minimumX, box.maximumX, margin), move.y,
	        SoftEdgeAxis(point.z, move.z, box.minimumZ, box.maximumZ, margin)};
}

/// Return the ease of a flight: 0 at the start, 1 at the end, with no speed and no acceleration at both ends
/// (the "smootherstep" curve).
[[nodiscard]] inline float FlightEase(float part)
{
	const auto t = std::clamp(part, 0.0f, 1.0f);
	return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

/// Return the direction of the view of the rig for a heading and a pitch.
[[nodiscard]] inline WorldPoint ViewDirection(float yaw, float pitch)
{
	return Rotate(HandRigRotation(yaw, pitch), WorldPoint {0.0f, 0.0f, 1.0f});
}

/// A view of the hand rig that a flight starts from or goes to. The camera is at `centre - view * distance`, with the
/// view direction of the heading and the view tilt.
struct FlightView
{
	WorldPoint centre;     ///< The point at the view centre that the camera looks at.
	float yaw = 0.0f;      ///< The heading, degrees.
	float pitch = 0.0f;    ///< The view tilt: the angle of the view centre below the horizon, degrees.
	float raise = 0.0f;    ///< The raise of the pitch control that gives this view tilt at the stage, degrees.
	float stage = 0.0f;    ///< The zoom stage.
	float distance = 0.0f; ///< The distance from the camera to the centre, metres.

	[[nodiscard]] WorldPoint Camera() const { return centre - ViewDirection(yaw, pitch) * distance; }
};

/// Return the view of a camera with the centre on its view line at `distance`.
[[nodiscard]] inline FlightView ViewOfCamera(WorldPoint camera, float yaw, float pitch, float raise, float stage,
                                             float distance)
{
	const auto length = std::max(distance, 0.01f);
	return {.centre = camera + ViewDirection(yaw, pitch) * length,
	        .yaw = yaw,
	        .pitch = pitch,
	        .raise = raise,
	        .stage = stage,
	        .distance = length};
}

/// The glide of the hand rig from one view to another view. The time of the flight is the only input, so the path
/// and the end do not depend on the frame rate. At the end, the view is exactly the target view.
struct CameraFlight
{
	FlightView from;
	FlightView to;
	float seconds = 0.6f; ///< The duration of the flight.
	float elapsed = 0.0f;

	/// Add the time of a frame. Return the part of the flight that is done, from 0 to 1 (before the ease).
	float Step(float deltaTime)
	{
		elapsed = std::min(elapsed + std::max(deltaTime, 0.0f), std::max(seconds, 0.0f));
		return Done();
	}

	[[nodiscard]] float Done() const { return seconds > 0.0f ? std::clamp(elapsed / seconds, 0.0f, 1.0f) : 1.0f; }
	[[nodiscard]] bool Finished() const { return Done() >= 1.0f; }

	/// Return the view at this time of the flight. The centre moves on a line, the heading on the shorter arc, the
	/// distance and the height of the zoom stage on a logarithmic scale, and the raise linearly. The view tilt comes
	/// from the raise and the stage with `pitchAt`, so that the view is the view of the pitch control at each time.
	/// At the start and at the end, the view tilt is the tilt of the view.
	template <typename PitchAt>
	[[nodiscard]] FlightView At(std::span<const StageView> table, PitchAt&& pitchAt) const
	{
		const auto ease = FlightEase(Done());
		if (ease <= 0.0f)
		{
			return from;
		}
		if (ease >= 1.0f)
		{
			return to;
		}
		FlightView view;
		view.centre = from.centre + (to.centre - from.centre) * ease;
		view.yaw = WrapDegrees(from.yaw + WrapDegrees(to.yaw - from.yaw) * ease);
		view.raise = from.raise + (to.raise - from.raise) * ease;
		view.distance = std::exp(std::log(std::max(from.distance, 0.01f)) +
		                         (std::log(std::max(to.distance, 0.01f)) - std::log(std::max(from.distance, 0.01f))) * ease);
		if (table.size() >= 2 && std::abs(to.stage - from.stage) > 1e-6f)
		{
			const auto low = std::log(HeightOfStage(table, from.stage));
			const auto high = std::log(HeightOfStage(table, to.stage));
			view.stage = StageOfHeight(table, std::exp(low + (high - low) * ease));
		}
		else
		{
			view.stage = from.stage + (to.stage - from.stage) * ease;
		}
		// The tilt of the pitch control at this raise and stage, with the difference of the start and of the end
		// views eased out. Then the start and the end are exact, also when the tilt of a view does not come from
		// the pitch control (for example a view with a pitch of the game).
		const auto tilt = pitchAt(view.raise, view.stage);
		const auto startError = from.pitch - pitchAt(from.raise, from.stage);
		const auto endError = to.pitch - pitchAt(to.raise, to.stage);
		view.pitch = tilt + startError * (1.0f - ease) + endError * ease;
		return view;
	}
};

/// A saved view of the hand rig.
struct CameraSpot
{
	WorldPoint position; ///< The camera with no obstacles (the rig), not the camera on the spring arm.
	float yaw = 0.0f;
	float pitch = 0.0f;
	float raise = 0.0f;
	float stage = 0.0f;
	WorldPoint centre; ///< The point at the view centre that the camera looks at.

	friend bool operator==(const CameraSpot& a, const CameraSpot& b)
	{
		const auto same = [](WorldPoint p, WorldPoint q) { return p.x == q.x && p.y == q.y && p.z == q.z; };
		return same(a.position, b.position) && a.yaw == b.yaw && a.pitch == b.pitch && a.raise == b.raise &&
		       a.stage == b.stage && same(a.centre, b.centre);
	}
};

/// The count of camera spots for each save.
constexpr size_t k_CameraSpotCount = 4;

/// The camera spots of each save, by a key of the save.
using CameraSpotBook = std::map<std::string, std::array<std::optional<CameraSpot>, k_CameraSpotCount>, std::less<>>;

/// Write the spots as JSON: `{"version": 1, "saves": {"<key>": [spot or null, ...]}}`.
[[nodiscard]] nlohmann::json CameraSpotsJson(const CameraSpotBook& book);

/// Read the spots from the JSON text of `CameraSpotsJson`. An empty text gives no spots. Return an error if the text
/// is not correct, so that the mod does not write over a file that it cannot read.
[[nodiscard]] moa::Result<CameraSpotBook> ParseCameraSpots(std::string_view text);

} // namespace moa_mod::camera
