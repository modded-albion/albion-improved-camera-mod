/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <cmath>

#include <algorithm>
#include <format>
#include <utility>

#include <moa_mod/camera/CameraLoop.h>

namespace moa_mod::camera
{

namespace
{
/// The land height query of the game gives the highest land near a point plus this height, metres.
constexpr float k_GroundQueryAboveLand = 2.0f;
/// The time in which the camera goes down to a lower land, seconds. A higher land takes effect at once.
constexpr float k_GroundFallSeconds = 0.3f;
/// The time in which the camera goes down after an obstacle under it, seconds.
constexpr float k_GroundLiftSeconds = 0.3f;
/// The radius of the sphere that the camera moves in the ground range: the radius of the collision test of the
/// camera of the game, metres.
constexpr float k_GroundRadius = 0.6f;
/// A hit nearer than this to the start of the sphere is an object that the sphere started in, metres.
constexpr float k_GroundOverlap = 1e-3f;
/// A move shorter than this is no move, metres.
constexpr float k_GroundMoveTolerance = 1e-4f;
/// The land drag of the game near the land moves the camera in each second by at most this multiple of the horizontal
/// distance from the camera to the grabbed land.
constexpr float k_GroundDragRate = 4.0f;
/// The ray of the land drag must go down by at least this part of its length, so that it meets the grabbed land in
/// front of the camera.
constexpr float k_GroundDragRayDown = 1e-3f;
/// The game holds the land under the cursor while the fade of its drag is above this value.
constexpr float k_DragFadeThreshold = 0.01f;
/// The largest change of a camera position or rotation that the rig accepts as its own camera.
constexpr float k_PlacementTolerance = 1e-3f;

[[nodiscard]] bool SamePlacement(const CameraPlacement& a, const CameraPlacement& b)
{
	const auto dot = std::abs(a.rotation.x * b.rotation.x + a.rotation.y * b.rotation.y + a.rotation.z * b.rotation.z +
	                          a.rotation.w * b.rotation.w);
	return Length(a.position - b.position) <= k_PlacementTolerance && dot >= 1.0f - k_PlacementTolerance;
}

[[nodiscard]] float HorizontalLength(WorldPoint point)
{
	return std::hypot(point.x, point.z);
}
} // namespace

bool CameraLoop::GroundLookOn() const
{
	return _settings.groundLook && _settings.pitchMode == PitchMode::Hand && !_groundCollisionsFailed &&
	       _stageTable.size() >= 2 && static_cast<float>(_settings.groundEyeHeight) < _stageTable.front().height;
}

float CameraLoop::ZoomGroundHeight() const
{
	return GroundLookOn() ? static_cast<float>(_settings.groundEyeHeight) : 0.0f;
}

StageLimits CameraLoop::RigZoomLimits(StageLimits limits) const
{
	// The ground range continues below the closest zoom stage of the game. A smallest stage of the game that is more
	// than zero (for example a clamp region) keeps its limit.
	if (GroundLookOn() && limits.minimum <= 0.0f)
	{
		limits.minimum = -k_GroundStages;
	}
	return limits;
}

bool CameraLoop::EnterGround(CameraGame& game)
{
	// The camera that the user sees is the start of the ground range: the camera does not move. The spring arm stops,
	// because in the ground range the camera does not turn about a far pivot.
	auto& rig = *_rig;
	auto camera = ShownCamera();
	auto highest = game.HighestGround(camera);
	if (!highest)
	{
		return false;
	}
	const auto land = *highest - k_GroundQueryAboveLand;
	// The hand rig keeps its camera this high above the land, so the top of the range is at least this height. Then
	// a zoom out of the ground range gives a camera that the land rule of the hand rig does not move.
	const auto lowestTop = std::max(static_cast<float>(_settings.groundEyeHeight),
	                                k_GroundQueryAboveLand + static_cast<float>(_settings.groundClearance));
	camera.y = std::max(camera.y, land + lowestTop);
	rig.position = camera;
	ResetArm();
	// The rig is now the camera that the user sees, so the chase and the land drag start again.
	_chase.Reset();
	_grip.reset();
	_ground = GroundState {.land = land, .topHeight = camera.y - land, .lift = 0.0f};
	_level = land;
	game.Debug(std::format("camera mod: the camera goes into the ground range {:.2f} m above the land", camera.y - land));
	return true;
}

bool CameraLoop::StepGround(CameraGame& game, const GroundStep& step)
{
	auto& rig = *_rig;
	auto& ground = *_ground;
	const auto eye = static_cast<float>(_settings.groundEyeHeight);
	const auto lowestTop = std::max(eye, k_GroundQueryAboveLand + static_cast<float>(_settings.groundClearance));
	const auto blend = GroundBlend(step.from);
	auto position = rig.position;

	// The turn and the pitch of the user move the camera about a point between the pivot of the gesture (the hand rig)
	// and the camera itself (a person that stands on the land). The blend has no step at the top of the range.
	const auto centre = step.pivot ? *step.pivot + (position - *step.pivot) * blend : position;
	if (step.yawStep != 0.0f)
	{
		position = YawAboutPivot(position, centre, step.yawStep);
	}
	if (step.userPitch != 0.0f && blend < 1.0f)
	{
		const auto before = position.y;
		position = PitchAboutPivot(position, centre, step.yaw, step.userPitch);
		// The pitch changes the height of the camera above the land. The top of the range changes with it, so a zoom
		// continues from this height.
		const auto height = GroundHeight(ground.topHeight, eye, step.from) + (position.y - before);
		if (auto top = GroundTopHeight(height, eye, step.from))
		{
			ground.topHeight = std::max(*top, lowestTop);
		}
	}
	if (_focusBox)
	{
		// The camera never leaves the map.
		const auto inside = ClampToBox(position, *_focusBox);
		position = {inside.x, position.y, inside.z};
	}

	// The height: the smoothed land under the camera, plus the height of the zoom in the range.
	const auto lastLand = ground.land;
	auto highest = game.HighestGround(position);
	if (!highest)
	{
		return false;
	}
	ground.land = FollowLand(lastLand, *highest - k_GroundQueryAboveLand, _deltaTime, k_GroundFallSeconds);
	const auto height = GroundHeight(ground.topHeight, eye, step.to);
	ground.lift = FollowValue(ground.lift, 0.0f, _deltaTime, k_GroundLiftSeconds);
	const WorldPoint wanted {position.x, ground.land + height + ground.lift, position.z};

	// With the object collision on, the objects stop the camera, and it slides along them.
	auto reached = SweepGround(game, step.start, wanted);
	if (HorizontalLength(reached - wanted) > k_GroundMoveTolerance)
	{
		auto there = game.HighestGround(reached);
		if (!there)
		{
			return false;
		}
		ground.land = FollowLand(lastLand, *there - k_GroundQueryAboveLand, _deltaTime, k_GroundFallSeconds);
	}
	// The land rule has priority: the camera is never below the eye height above the land.
	const auto floor = ground.land + height;
	reached.y = std::max(reached.y, floor);
	ground.lift = reached.y - floor;
	rig.position = reached;
	_level = ground.land;

	_trace.groundBlend = GroundBlend(step.to);
	_trace.eyeHeight = reached.y - ground.land;
	_trace.zoomHeight = height;
	_trace.pivotKept = false;
	return true;
}

WorldPoint CameraLoop::SweepGround(CameraGame& game, WorldPoint from, WorldPoint to)
{
	if (!_settings.objectCollision)
	{
		// With the object collision off, only the land keeps the camera out, and the walk goes through the objects.
		return to;
	}
	const auto margin = static_cast<float>(_settings.collisionMargin);
	// The distance that the camera can move on `move` before it is `margin` from an object. Nothing if the game cannot
	// do the sphere test.
	auto freeDistance = [&](WorldPoint origin, WorldPoint move) -> std::optional<float> {
		const auto length = Length(move);
		if (length <= k_GroundMoveTolerance)
		{
			return length;
		}
		auto hit = game.CastSphere(origin, move * (1.0f / length), k_GroundRadius, length + margin, k_CameraObstacleLayers);
		if (!hit)
		{
			return std::nullopt;
		}
		if (!hit->hit || hit->distance <= k_GroundOverlap)
		{
			// No object, or the sphere started in an object: the camera can move out of it.
			return length;
		}
		return std::clamp(hit->distance - margin, 0.0f, length);
	};
	const auto move = to - from;
	const auto length = Length(move);
	auto free = freeDistance(from, move);
	if (!free || *free >= length - k_GroundMoveTolerance)
	{
		// With no sphere test, only the land keeps the camera out.
		return to;
	}
	auto position = from + move * (*free / length);
	// The camera slides along the object: it does the remaining move on each axis that is free.
	for (int axis = 0; axis < 3; ++axis)
	{
		const auto rest = to - position;
		const WorldPoint part {axis == 0 ? rest.x : 0.0f, axis == 2 ? rest.y : 0.0f, axis == 1 ? rest.z : 0.0f};
		const auto partLength = Length(part);
		if (partLength <= k_GroundMoveTolerance)
		{
			continue;
		}
		auto partFree = freeDistance(position, part);
		if (!partFree)
		{
			break;
		}
		position = position + part * (*partFree / partLength);
	}
	return position;
}

WorldPoint CameraLoop::GuardGroundDrag(CameraGame& game, WorldPoint shift)
{
	// Near the land the camera does not go up or down with the focus of the game: only the horizontal move counts.
	const WorldPoint horizontal {shift.x, 0.0f, shift.z};
	const auto fields = game.Drag();
	if (fields.fade <= k_DragFadeThreshold || HorizontalLength(horizontal) <= 0.0f)
	{
		return horizontal;
	}
	// The land drag of the game meets the ray of its hold cursor with the level of the grabbed land. Near the land the
	// ray is almost level. If it does not go down to the grabbed land in front of the camera, the meeting point is not
	// the land that the user holds, and the camera would jump far.
	auto ray = game.ScreenRay(fields.holdCursor);
	if (!ray || ray->direction.y > -k_GroundDragRayDown || ray->origin.y <= fields.grab.y)
	{
		game.Debug("camera mod: near the land, the camera does not take a land drag move that does not hold the land");
		return {};
	}
	// Near the horizon a small move of the cursor moves the meeting point far. The camera moves at most a part of
	// its distance to the grabbed land in each second, and the game holds the land again in its next frame, so the
	// camera continues to the held place smoothly.
	const auto toGrab = HorizontalLength(fields.grab - (ray->origin - horizontal));
	const auto largest = k_GroundDragRate * std::max(toGrab, static_cast<float>(_settings.groundEyeHeight)) * _deltaTime;
	const auto length = HorizontalLength(horizontal);
	return length > largest ? horizontal * (largest / length) : horizontal;
}

bool CameraLoop::ResumeGround(const CameraPlacement& camera)
{
	auto resume = std::exchange(_groundResume, std::nullopt);
	if (!resume || !GroundLookOn() || !SamePlacement(camera, resume->camera))
	{
		return false;
	}
	// Nothing moved the camera while the mod stood back (for example, the game was paused). The rig continues in the
	// ground range from the same view.
	_rig = resume->rig;
	_ground = resume->ground;
	_zoom.value = resume->zoomValue;
	_zoom.target = resume->zoomTarget;
	_pitchTarget = resume->pitchTarget;
	_pitchValue = resume->pitchValue;
	_heading.Reset(_rig->yaw);
	_lastStage = _zoom.value;
	_level = _ground->land;
	return true;
}

} // namespace moa_mod::camera
