/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <cmath>

#include <algorithm>
#include <format>

#include <core/CameraLoop.h>

namespace moa_mod::camera
{

namespace
{
/// The land drag stops the camera this distance before the land in its way, metres.
constexpr float k_GripLandStop = 3.0f;
/// A limit of the land drag that removes less than this distance does not count as a limit, metres.
constexpr float k_GripLimitTolerance = 1e-3f;
/// The chase is two times slower for this part of its time after a limit moved the target.
constexpr float k_ChaseSlowFactor = 2.0f;
/// The sphere that keeps the camera of the chase in front of the objects: the sphere of the spring arm, metres.
constexpr float k_ChaseObstacleRadius = 0.6f;
/// A lag shorter than this does not need the object test, metres.
constexpr float k_ChaseObstacleTolerance = 0.01f;
/// A pivot of the spring arm deeper than this under the land goes back onto the land, metres.
constexpr float k_PivotUnderLand = 0.05f;
} // namespace

bool CameraLoop::LandGripOn() const
{
	// In the ground range the camera walks on the land, and the drag of the game moves it (`GuardGroundDrag`).
	return _settings.landGrip && _settings.pitchMode == PitchMode::Hand && !_ground;
}

void CameraLoop::ResetMotion()
{
	_chase.Reset();
	_chaseSlowLeft = 0.0f;
	_grip.reset();
	_gripStarted = false;
	_gripRetake = false;
	_gripRefused = false;
	_turnInput.Reset();
	_pitchInput.Reset();
}

bool CameraLoop::KeepArmPivotOnLand(CameraGame& game)
{
	if (!_armPivot || !_rig || _pivotLimited)
	{
		return true;
	}
	auto land = game.LandHeight(*_armPivot);
	if (!land)
	{
		return false;
	}
	if (*land <= _armPivot->y + k_PivotUnderLand)
	{
		return true;
	}
	// The pivot under the land makes the arm find the land near the pivot, and the camera jumps to it. The new arm goes
	// from the pivot on the land through the camera on the arm, so that camera does not move. The rig keeps the part of
	// the distance that the obstacles took.
	const WorldPoint raised {_armPivot->x, *land, _armPivot->z};
	auto& rig = *_rig;
	const auto onArm = ArmCamera();
	const auto excess = std::max(Length(rig.position - *_armPivot) - _armLength, 0.0f);
	const auto toCamera = onArm - raised;
	const auto length = Length(toCamera);
	if (length <= k_PivotUnderLand)
	{
		return true;
	}
	if (_pivot && _pivot->x == _armPivot->x && _pivot->y == _armPivot->y && _pivot->z == _armPivot->z)
	{
		_pivot = raised;
	}
	_armPivot = raised;
	_armLength = length;
	rig.position = excess > 0.0f ? raised + toCamera * ((length + excess) / length) : onArm;
	_desired = Length(rig.position - raised);
	return true;
}

bool CameraLoop::StepGrip(CameraGame& game, bool rightHeld, bool rightPressed, WorldPoint& move)
{
	move = {};
	_gripStarted = false;
	if (!LandGripOn() || !rightHeld)
	{
		_grip.reset();
		_gripRetake = false;
		_gripRefused = false;
		return true;
	}
	if (rightPressed)
	{
		// The game does not drag the land for a press on its UI.
		auto hand = game.Hand();
		_gripRefused = hand && hand->overUi;
	}
	if (_gripRefused)
	{
		return true;
	}
	auto cursor = game.InputPosition();
	if (!cursor)
	{
		return true;
	}
	if (std::exchange(_gripRetake, false))
	{
		_grip.reset();
	}
	auto& rig = *_rig;
	if (!_grip)
	{
		// The grip is the land under the cursor in the view that the user sees.
		const auto rotation = HandRigRotation(rig.yaw, rig.pitch);
		if (!game.SetCamera({.position = ShownCamera(), .rotation = rotation}))
		{
			return false;
		}
		auto land = game.LandUnderScreenPoint(*cursor);
		auto fieldOfView = game.FieldOfView();
		auto screen = game.ScreenSize();
		if (!land || !fieldOfView || !screen || screen->x <= 0.0f || screen->y <= 0.0f)
		{
			// No land under the cursor (the sky): the drag starts when the cursor comes onto the land.
			return true;
		}
		const auto origin = TargetCamera();
		auto under = game.LandHeight(origin);
		if (!under)
		{
			return false;
		}
		_grip = LandGrip {.point = *land,
		                  .origin = origin,
		                  .rotation = rotation,
		                  .cursor = *cursor,
		                  .fieldOfView = *fieldOfView,
		                  .screen = *screen,
		                  .plane = GripPlaneThrough(*land, origin, *under),
		                  // The limit of the move for each pixel grows with the zoom: the distance to the focus of the game.
		                  .distance = Length(_focusWritten - origin),
		                  .applied = {}};
		_gripStarted = true;
		_trace.gripActive = true;
		Tell(CameraEventKind::Gesture, std::format("land grip at {}", FormatPoint(*land)));
		return true;
	}
	auto& grip = *_grip;
	_trace.gripActive = true;
	const ScreenCamera camera {.position = grip.origin,
	                           .rotation = grip.rotation,
	                           .verticalFieldOfView = grip.fieldOfView,
	                           .width = grip.screen.x,
	                           .height = grip.screen.y};
	const auto rayNow = ScreenRay(camera, {cursor->x, cursor->y}).direction;
	const auto rayAtGrip = ScreenRay(camera, {grip.cursor.x, grip.cursor.y}).direction;
	const auto pixels = std::hypot(cursor->x - grip.cursor.x, cursor->y - grip.cursor.y);
	const GripLimits limits {.stepPerPixel = static_cast<float>(_settings.gripStepPerPixel),
	                         .minimumStep = static_cast<float>(_settings.gripMinimumStep)};
	auto landMove = GripLandMove(grip.plane, grip.origin, rayNow, rayAtGrip, pixels, grip.distance, limits);
	if (!landMove)
	{
		// The cursor is above the horizon of the plane: the land stays where the drag put it.
		return true;
	}
	const auto unlimited = GripLandMove(grip.plane, grip.origin, rayNow, rayAtGrip, pixels, grip.distance,
	                                    {.stepPerPixel = 1e9f, .minimumStep = 1e9f});
	// The camera moves by the opposite of the move of the land.
	const auto wanted = WorldPoint {} - *landMove;
	const auto from = TargetCamera();
	auto end = StopBeforeLand(from, from + (wanted - grip.applied), k_GripLandStop,
	                          [&game](WorldPoint point) { return game.LandHeight(point); });
	if (!end)
	{
		return false;
	}
	move = *end - from;
	grip.applied = grip.applied + move;
	_trace.gripMove = move;
	_trace.gripLimited = (unlimited && Length(*unlimited - *landMove) > k_GripLimitTolerance) ||
	                     Length(grip.applied - wanted) > k_GripLimitTolerance;
	return true;
}

float CameraLoop::LargestStep(float focusDistance) const
{
	// The limit grows with the distance to the focus, so that it is the same at each zoom.
	return std::max(static_cast<float>(_settings.chaseMinSpeed), static_cast<float>(_settings.chaseMaxSpeed) * focusDistance) *
	       _deltaTime;
}

bool CameraLoop::ChaseOn() const
{
	// Without the spring arm the push and the collision test of the game act on the camera, and they start from the
	// camera that the user sees. The chase then fights them, so it is off.
	return _settings.chaseSeconds > 0.0 && _settings.pitchMode == PitchMode::Hand && _settings.springArm && !_ground;
}

void CameraLoop::StepChase(CameraGame& game, WorldPoint targetMove)
{
	_chaseMoving = false;
	if (!ChaseOn())
	{
		_chase.Reset();
		_chaseSlowLeft = 0.0f;
		return;
	}
	_chaseMoving = Length(targetMove) > 0.0f || !_chase.Still();
	if (!_chaseMoving)
	{
		return;
	}
	auto seconds = static_cast<float>(_settings.chaseSeconds);
	if (_chaseSlowLeft > 0.0f)
	{
		seconds *= k_ChaseSlowFactor;
		_chaseSlowLeft = std::max(_chaseSlowLeft - _deltaTime, 0.0f);
	}
	// The limit of the speed stops a leap.
	const auto maximumSpeed = _deltaTime > 0.0f ? LargestStep(Length(TargetCamera() - _focusWritten)) / _deltaTime : 0.0f;
	_chase.Step(targetMove, _deltaTime, seconds, maximumSpeed);
	if (!_chase.Still())
	{
		// The target is clear of the land (and of the objects, with the object collision). The path of the chase can cut
		// a corner of that path, so the camera that the user sees stays above the floor and in front of the objects too.
		const auto target = TargetCamera();
		// The camera that the user sees is at least as high above its land as the target is above its land, up to the
		// ground clearance, and never under its land.
		const auto shown = target + _chase.lag;
		auto landShown = game.LandHeight(shown);
		auto landTarget = game.LandHeight(target);
		if (landShown && landTarget)
		{
			const auto clearance = std::clamp(target.y - *landTarget, 0.0f, static_cast<float>(_settings.groundClearance));
			const auto lowest = *landShown + clearance;
			if (shown.y < lowest)
			{
				_chase.lag.y = lowest - target.y;
				_chase.velocity.y = std::max(_chase.velocity.y, 0.0f);
			}
		}
		if (_settings.objectCollision)
		{
			const auto length = Length(_chase.lag);
			if (length > k_ChaseObstacleTolerance)
			{
				const auto direction = _chase.lag * (1.0f / length);
				auto hit = game.CastSphere(target, direction, k_ChaseObstacleRadius, length, k_CameraObstacleLayers);
				if (hit && hit->hit)
				{
					// The obstacle stops the lag: the camera stays on the side of the target.
					_chase.lag = direction * std::max(hit->distance - k_ChaseObstacleTolerance, 0.0f);
					_chase.velocity = {};
				}
			}
		}
	}
	if (!_chase.Still())
	{
		_trace.chaseLag = _chase.lag;
		// The camera that the user sees is not at the target, so the pivot is not at its place on the screen.
		_trace.pivotKept = false;
	}
	_trace.chaseSpeed = Length(_chase.velocity);
}

} // namespace moa_mod::camera
