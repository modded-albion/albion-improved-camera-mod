/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <cmath>

#include <algorithm>
#include <format>
#include <utility>

#include <core/CameraLoop.h>

namespace moa_mod::camera
{

namespace
{
constexpr float k_MaxDeltaTime = 0.1f;
constexpr float k_StageTolerance = 1e-4f;
constexpr float k_TurnTolerance = 1e-4f;
constexpr int32_t k_MiddleMouseButton = 2;
constexpr int32_t k_RightMouseButton = 1;
constexpr float k_LookUpEaseSeconds = 0.4f;
constexpr float k_TiltTolerance = 1e-3f;
constexpr float k_MinimumDistanceScale = 0.5f;
constexpr float k_MaximumDistanceScale = 2.0f;
constexpr double k_DoubleClickSeconds = 0.35;
constexpr float k_DoubleClickPixels = 8.0f;
/// The game holds the land under the cursor while the fade of its drag is above this value.
constexpr float k_DragFadeThreshold = 0.01f;
/// The largest change of a camera position or rotation that the loop accepts as its own camera.
constexpr float k_PlacementTolerance = 1e-3f;
/// The age, in seconds, at which a frame of the drag has approximately 37 percent of its weight in the speed.
constexpr float k_DragVelocitySeconds = 0.1f;
/// The glide stops when its speed is this part of the camera height for each second, or less.
constexpr float k_GlideStopHeights = 0.01f;
/// The largest move of the focus by the game, metres, that the glide accepts as no move.
constexpr float k_GlideTolerance = 1e-3f;
/// The hand stops the ease to the cursor when it is nearer than this number of pixels.
constexpr float k_HandEasePixels = 0.5f;
/// The game sets the fade of its land drag to 1 while the user holds the land.
constexpr float k_DragFadeHeld = 1.0f - 1e-4f;
/// With the turn lock, a frame of a middle button drag does not turn when its horizontal move is less than this
/// part of its vertical move.
constexpr float k_TurnLockRatio = 0.5f;

[[nodiscard]] bool SamePlacement(const CameraPlacement& a, const CameraPlacement& b)
{
	const auto dot = std::abs(a.rotation.x * b.rotation.x + a.rotation.y * b.rotation.y + a.rotation.z * b.rotation.z +
	                          a.rotation.w * b.rotation.w);
	return Length(a.position - b.position) <= k_PlacementTolerance && dot >= 1.0f - k_PlacementTolerance;
}

[[nodiscard]] bool SamePoint(WorldPoint a, WorldPoint b)
{
	return a.x == b.x && a.y == b.y && a.z == b.z;
}

[[nodiscard]] bool IsZero(WorldPoint point)
{
	return point.x == 0.0f && point.y == 0.0f && point.z == 0.0f;
}

[[nodiscard]] nlohmann::json ToJson(WorldPoint point)
{
	return nlohmann::json::array({point.x, point.y, point.z});
}

[[nodiscard]] nlohmann::json ToJson(const std::optional<WorldPoint>& point)
{
	return point ? ToJson(*point) : nlohmann::json();
}
} // namespace

CameraLoop::CameraLoop(CameraSettings settings)
    : _settings(settings)
{
}

void CameraLoop::ApplySettings(CameraSettings settings)
{
	CameraLoop next(settings);
	if (_handBack)
	{
		// The mod stands back, for example in the pause menu. The new loop has the same return as the old loop, so it
		// continues from the view of the user if nothing moved it.
		next._handBack = true;
		next._standBackCamera = _standBackCamera;
		next._standBackSpot = settings.flyBackAfterStandBack ? _standBackSpot : std::nullopt;
		next._gameStateWritten = _gameStateWritten;
	}
	*this = std::move(next);
}

void CameraLoop::StandBack()
{
	// The other camera code of the game starts from the camera that the user sees.
	if (!_handBack)
	{
		// The first frame of the stand-back. Keep the camera that the mod wrote and the view of the user, for the
		// return.
		_handBack = true;
		_standBackCamera = _written;
		_standBackSpot = _settings.flyBackAfterStandBack ? CurrentSpot() : std::nullopt;
	}
	_frameActive = false;
	_resync = true;
	_trace = {};
	if (_ground && _rig && _written)
	{
		// Keep the ground range. If nothing moves the camera while the mod stands back (for example, a pause), the rig
		// continues from the same view.
		_groundResume = GroundResume {.camera = *_written,
		                              .ground = *_ground,
		                              .rig = *_rig,
		                              .zoomValue = _zoom.value,
		                              .zoomTarget = _zoom.target,
		                              .pitchTarget = _pitchTarget,
		                              .pitchValue = _pitchValue};
	}
	_ground.reset();
	_written.reset();
	_gameCamera.reset();
	StopGlide();
	StopTurnGlides();
	ResetMotion();
	// The cursor is free while the mod stands back. If the user still holds the button after that, the rig does not
	// move the cursor back.
	if (_cursorLock)
	{
		_cursorLockOff = CursorLockOff::StandBack;
	}
	_cursorLock.reset();
}

bool CameraLoop::Before(CameraGame& game, float deltaTime, double seconds)
{
	_deltaTime = std::clamp(deltaTime, 0.0f, k_MaxDeltaTime);
	_seconds = seconds;
	_frameActive = false;
	_trace = {};
	StopHeldObjectZoom(game);
	const auto active = _settings.pitchMode == PitchMode::Hand ? BeforeHand(game) : BeforeOrbit(game);
	if (active)
	{
		// The game pans and zooms with the keys in its frame, from the camera that it has now.
		if (auto camera = game.Camera())
		{
			_startRotation = camera->rotation;
		}
		ScalePan(game);
		SmoothKeyZoom(game);
	}
	FlushEvents(game);
	return active;
}

bool CameraLoop::BeforeOrbit(CameraGame& game)
{
	_startDirection = game.Direction();
	const auto stage = game.Stage();
	if (_resync)
	{
		_heading.Reset(_startDirection);
		_zoom.Reset(stage);
		ResetPitch();
		_lastStage = stage;
		_resync = false;
	}
	_writtenStage = stage;
	BlockPitchDragHold(game);
	RestoreGameCamera(game);
	StepGlide(game);

	if (_settings.smoothZoom)
	{
		if (_settings.continuousZoom && _stageTable.empty())
		{
			// Without the table, the wheel zooms by stages.
			static_cast<void>(LoadStageTable(game));
		}
		auto limits = ZoomLimits(game);
		if (!limits)
		{
			_resync = true;
			return false;
		}
		auto hand = game.Hand();
		const auto overUi = hand && hand->overUi;
		_lastWheel = 0.0f;
		if (!overUi)
		{
			auto wheel = game.WheelAxis();
			if (!wheel)
			{
				_resync = true;
				return false;
			}
			_lastWheel = *wheel;
		}
		if (_lastWheel != 0.0f)
		{
			// The loop uses this wheel step. The game must not use it too, so the game ignores the wheel in this
			// frame. `EndFrame` puts the old value back.
			auto previous = game.IgnoreWheel();
			if (previous && game.SetIgnoreWheel(true))
			{
				_wheelToRestore = *previous;
				AddWheelToTarget(_lastWheel, *limits);
			}
		}
		_writtenStage = StepZoom(*limits, false);
		game.SetStage(_writtenStage);
	}
	_trace.writtenStage = _writtenStage;
	_trace.zoomTarget = _zoom.target;
	_frameActive = true;
	return true;
}

bool CameraLoop::EndFrame(CameraGame& game)
{
	RestoreWheel(game);
	RestoreMoveSpeed(game);
	RestoreDollySmooth(game);
	RestoreEases(game);
	RestoreGameCollisions(game);
	RestoreHeldObjectZoom(game);
	return std::exchange(_frameActive, false);
}

void CameraLoop::StopHeldObjectZoom(CameraGame& game)
{
	// While the hand holds an object, the game keeps the zoom stage at a smallest stage, so a pickup near the ground
	// zooms out. The user does not want this zoom while the mod is on. The game reads the smallest stage in its frame,
	// so the loop sets it to zero for the frame, and `EndFrame` puts the value of the game back.
	auto stage = game.HeldObjectMinimumStage();
	if (!stage)
	{
		return;
	}
	if (!_gameHeldMinimum || *stage != 0.0f)
	{
		// Keep the value of the game. If a frame stopped before `EndFrame`, the value is still zero, and the loop
		// puts back the value that it kept.
		_gameHeldMinimum = *stage;
	}
	if (game.SetHeldObjectMinimumStage(0.0f))
	{
		_heldMinimumToRestore = *_gameHeldMinimum;
		_heldMinimumStopped = true;
	}
}

void CameraLoop::RestoreHeldObjectZoom(CameraGame& game)
{
	_heldMinimumStopped = false;
	if (!_heldMinimumToRestore)
	{
		return;
	}
	const auto stage = *_heldMinimumToRestore;
	_heldMinimumToRestore.reset();
	if (!game.SetHeldObjectMinimumStage(stage))
	{
		game.ReportError("the mod could not put back the zoom limit of a held object");
	}
}

std::optional<StageLimits> CameraLoop::ZoomLimits(CameraGame& game) const
{
	auto limits = game.ZoomLimits();
	if (limits && _heldMinimumStopped)
	{
		// The smallest stage of the game comes only from a held object. In the first frame after a stop, the game
		// still has the value of the last frame.
		limits->minimum = 0.0f;
	}
	return limits;
}

bool CameraLoop::After(CameraGame& game)
{
	RemoveEdgeScroll(game);
	if (_settings.pitchMode == PitchMode::Hand)
	{
		const auto done = AfterHand(game);
		if (done)
		{
			UpdateGesture(game);
		}
		else
		{
			_resync = true;
		}
		FlushEvents(game);
		return done;
	}
	CheckGlide(game);
	if (!ApplyView(game))
	{
		_resync = true;
		return false;
	}
	TrackPosition(game, game.Focus());
	EaseHand(game);
	return true;
}

void CameraLoop::Tell(CameraEventKind kind, std::string text)
{
	_trace.events.Append(text);
	_events.emplace_back(kind, std::move(text));
}

void CameraLoop::FlushEvents(CameraGame& game)
{
	for (auto& [kind, text] : _events)
	{
		game.Event(kind, std::move(text));
	}
	_events.clear();
}

void CameraLoop::UpdateGesture(CameraGame& game)
{
	const auto& input = _frameInput;
	const auto held =
	    _gesture == Gesture::Hold || _gesture == Gesture::Turn || _gesture == Gesture::Pitch || _gesture == Gesture::TurnPitch;
	auto now = Gesture::None;
	if (_flight)
	{
		now = Gesture::Flight;
	}
	else if (_ground)
	{
		now = Gesture::Ground;
	}
	else if (input.right)
	{
		now = Gesture::LandDrag;
	}
	else if (input.middle)
	{
		// In a hold the gesture only grows: a hold, then a turn or a pitch, then both. A frame with no move does not
		// end the gesture.
		const auto keep = !input.pressed && held;
		const auto turned = input.turning || (keep && (_gesture == Gesture::Turn || _gesture == Gesture::TurnPitch));
		const auto pitched = input.pitching || (keep && (_gesture == Gesture::Pitch || _gesture == Gesture::TurnPitch));
		now = turned && pitched ? Gesture::TurnPitch : (turned ? Gesture::Turn : (pitched ? Gesture::Pitch : Gesture::Hold));
	}
	else if (_turnGlide.active || _pitchGlide.active || _glide.active)
	{
		now = Gesture::Glide;
	}
	else if (input.turning)
	{
		now = Gesture::Turn;
	}
	else if (input.zooming)
	{
		now = Gesture::Zoom;
	}
	_trace.gesture = now;
	const auto last = std::exchange(_gesture, now);
	if (now == last && !(input.pressed && now != Gesture::None))
	{
		return;
	}
	// The text of the event. It is made only at a change, so a frame with no change does not allocate memory.
	std::string text;
	if (last != Gesture::None && now != last)
	{
		text = std::format("gesture {} ends", Name(last));
	}
	if (now != Gesture::None)
	{
		if (!text.empty())
		{
			text += "; ";
		}
		text += std::format("gesture {} starts", Name(now));
		if (input.middle)
		{
			const auto source = game.UsedSource(k_MiddleButton);
			text += std::format(" (middle {}, source {}", input.pressed ? "press" : "held",
			                    source ? Name(*source) : std::string_view("none"));
			if (input.pressed && _trace.pivot)
			{
				text += std::format(", pivot {}", FormatPoint(*_trace.pivot));
			}
			text += ')';
		}
		else if (input.right)
		{
			text += " (right press)";
		}
		else if (now == Gesture::Turn)
		{
			text += " (keys)";
		}
	}
	Tell(CameraEventKind::Gesture, std::move(text));
}

void CameraLoop::RestoreWheel(CameraGame& game)
{
	if (!_wheelToRestore)
	{
		return;
	}
	const auto value = *_wheelToRestore;
	_wheelToRestore.reset();
	if (!game.SetIgnoreWheel(value))
	{
		game.ReportError("the mod could not put back the wheel setting of the game");
	}
}

bool CameraLoop::ApplyView(CameraGame& game)
{
	const auto gameDirection = game.Direction();
	const auto gameStage = game.Stage();
	if (_settings.smoothZoom && std::abs(gameStage - _writtenStage) > k_StageTolerance)
	{
		// The game changed the stage itself (keys, a forced zoom, a jump to the cursor): follow the game.
		_zoom.Reset(gameStage);
	}

	const auto input = ReadPitchInput(game);
	// The game turns by the horizontal move of a middle button drag. A pitch drag is mostly vertical, and a small
	// horizontal move of the hand must not turn the camera. The lock cancels the turn of the game in such a frame.
	const auto turnLocked = _settings.pitchTurnLock && input.vertical != 0.0f &&
	                        std::abs(input.horizontal) < k_TurnLockRatio * std::abs(input.vertical);
	const auto turnedTo = turnLocked ? _startDirection : gameDirection;
	_trace.turnLocked = turnLocked && std::abs(WrapDegrees(gameDirection - _startDirection)) > k_TurnTolerance;
	auto direction = turnedTo;
	if (_settings.rotateSmoothing > 0.0)
	{
		direction = _heading.Step(_startDirection, turnedTo, _deltaTime, static_cast<float>(_settings.rotateSmoothing));
	}
	else
	{
		_heading.Reset(turnedTo);
	}

	// The pitch: one raise value gives the tilt and, near the ground, the look up.
	_appliedLookUp = 0.0f;
	const auto gameTilt = game.StageTilt();
	const auto stageHeight = game.StageHeight();
	const auto stageDistance = StageDistance(stageHeight, gameTilt);
	const auto inPlace = _settings.pitchMode == PitchMode::InPlace;
	const auto pitchSmoothing = static_cast<float>(_settings.pitchSmoothing);
	_range = PitchRangeFor(gameStage, gameTilt, PitchLimitsFromSettings());
	if (stageDistance <= 0.0f)
	{
		// The game gives no distance for this tilt. Keep the tilt of the game, and permit only the look up.
		_range = {.steepest = 0.0f, .flattest = 0.0f, .highest = _range.highest - _range.flattest};
	}
	if (input.reset || (_settings.pitchResetOnZoom && std::abs(gameStage - _lastStage) > k_StageTolerance))
	{
		_pitchTarget = 0.0f;
		_pitchReset = true;
		_pitchFromInput = false;
	}
	_lastStage = gameStage;
	if (input.raise != 0.0f)
	{
		_pitchReset = false;
		_pitchFromInput = true;
	}
	_pitchTarget = StepPitchTarget(_pitchTarget, input.raise, _range, _deltaTime, k_LookUpEaseSeconds);
	const auto previousView = SplitPitch(_pitchValue, gameTilt, _range);
	_pitchValue = FollowValue(_pitchValue, _pitchTarget, _deltaTime, pitchSmoothing);
	_view = SplitPitch(_pitchValue, gameTilt, _range);
	_trace.raise = _pitchValue;
	_trace.raiseTarget = _pitchTarget;
	if (inPlace)
	{
		// A view that turns in place keeps the height of the game. The distance follows from the tilt.
		_distanceScale = 1.0f;
	}
	else if (_pitchReset || stageDistance <= 0.0f)
	{
		_distanceScale = FollowValue(_distanceScale, 1.0f, _deltaTime, std::max(pitchSmoothing, k_LookUpEaseSeconds));
	}
	_distanceScale = std::clamp(_distanceScale, k_MinimumDistanceScale, k_MaximumDistanceScale);
	// In place, the tilt and the look up are one turn of the camera about its position.
	const auto viewTilt = inPlace ? _view.tilt - _view.lookUp : _view.tilt;
	const auto previousViewTilt = inPlace ? previousView.tilt - previousView.lookUp : previousView.tilt;
	_trace.viewTilt = viewTilt;
	const auto tiltChanged = stageDistance > 0.0f && (std::abs(viewTilt - gameTilt) > k_TiltTolerance ||
	                                                  std::abs(_distanceScale - 1.0f) > k_TiltTolerance);

	const auto drag = ReadDragFrame(game);
	std::optional<WorldPoint> pivot;
	const auto turned = std::abs(WrapDegrees(direction - _startDirection)) > k_TurnTolerance;
	if (_settings.pivotOnHand && turned)
	{
		pivot = HandPivot(game);
	}
	// With no hand point, the camera turns about the focus of the game, as the game turns it. In a view that turns
	// in place, the land at the view centre can be far away, and a turn about it moves the camera far.
	const auto handPivot = pivot.has_value();
	_trace.turned = turned;
	_trace.pivot = pivot;
	std::optional<WorldPoint> pitchPivot;
	if (!inPlace && _settings.pivotOnHand && _pitchFromInput && std::abs(_view.tilt - previousView.tilt) > k_TiltTolerance)
	{
		pitchPivot = handPivot ? pivot : HandPivot(game);
	}
	if (!pivot && std::abs(WrapDegrees(direction - gameDirection)) <= k_TurnTolerance && !tiltChanged && _view.lookUp <= 0.0f &&
	    _clearance <= 0.0f)
	{
		// The camera of the user is the camera of the game. The land drag of the game is then correct as it is.
		_written.reset();
		_gameCamera.reset();
		_drag.grab.reset();
		if (!drag.holding)
		{
			_drag.idlePoint = drag.gameGrab;
		}
		_dragActive = false;
		_anchor.reset();
		_viewFocus.reset();
		if (auto camera = game.Camera())
		{
			_shown = camera->position;
		}
		return true;
	}

	auto camera = game.Camera();
	if (!camera)
	{
		return false;
	}
	const CameraPose gamePose {
	    .focus = game.Focus(), .position = camera->position, .rotation = camera->rotation, .direction = gameDirection};
	auto start = gamePose;
	const auto dragFields = game.Drag();
	if (_dragBefore.pressPoint && !SamePoint(dragFields.lastCursorTerrain, _dragBefore.lastCursorTerrain))
	{
		// The game moved the focus to the land under the cursor (zoom to the cursor). It found that land with its own
		// camera. Use the land under the cursor in the view of the user.
		const auto jump = *_dragBefore.pressPoint - dragFields.lastCursorTerrain;
		start.focus = start.focus + jump;
		start.position = start.position + jump;
		_trace.cursorJump = jump;
		game.SetLastCursorTerrain(*_dragBefore.pressPoint);
	}
	auto pose = ChangeTurn(start, {.startDirection = _startDirection,
	                               .direction = direction,
	                               .pivot = pivot,
	                               .invertPivot = handPivot && _settings.invertRotate});
	// The camera of the game for the focus and the heading of the loop. The next frame of the game starts from it.
	const auto gameEquivalent = pose;
	const CameraRig gameRig {.focus = pose.focus, .direction = pose.direction, .tilt = gameTilt, .distance = stageDistance};
	// The rotation of the camera of the user for a view tilt, with the camera shake of the game.
	const auto inPlaceRotation = [&](float tilt) {
		const CameraRig view {.focus = {}, .direction = pose.direction, .tilt = tilt, .distance = 0.0f};
		return Multiply(Multiply(RigRotation(view), Inverse(RigRotation(gameRig))), gameEquivalent.rotation);
	};

	auto lookUp = _view.lookUp;
	_appliedLookUp = 0.0f;
	if (tiltChanged && inPlace)
	{
		// The camera of the user is at the position of the camera of the game, and turns about it. The game places
		// that camera from its own focus, so the game keeps the camera above the land, in the map and out of
		// objects, and a pitch does not move it. The loop does not write a different focus for the pitch or for a
		// zoom: a zoom moves the camera as the game moves it. A zoom that keeps the land at the view centre moves the
		// camera very far when the view is near the horizon.
		pose.rotation = inPlaceRotation(viewTilt);
		_appliedLookUp = gameTilt - viewTilt;
		lookUp = 0.0f;
	}
	else if (tiltChanged)
	{
		// Put the camera at the tilt and the distance of the loop. The loop adds the change to the transform of the
		// game, so the push of the game away from objects and the camera shake stay.
		auto current = gameRig;
		current.tilt = previousView.tilt;
		current.distance = stageDistance * _distanceScale;
		const auto rig = PitchRig(current, pitchPivot, _view.tilt, k_MinimumDistanceScale * stageDistance,
		                          k_MaximumDistanceScale * stageDistance);
		_distanceScale = rig.distance / stageDistance;
		pose.position = pose.position + (RigPosition(rig) - RigPosition(gameRig));
		pose.rotation = Multiply(Multiply(RigRotation(rig), Inverse(RigRotation(gameRig))), pose.rotation);
		pose.focus = rig.focus;
	}
	const auto keepAboveLand = (tiltChanged || _clearance > 0.0f) && !game.IgnoresCollisions();
	if (tiltChanged || _clearance > 0.0f)
	{
		// The game keeps its own camera above the land. A flatter camera is lower, so the loop raises it smoothly
		// to the clearance above the land height that the game gives.
		if (!keepAboveLand)
		{
			_clearance = 0.0f;
		}
		else
		{
			auto ground = game.HighestGround(pose.position);
			if (!ground)
			{
				return false;
			}
			const auto clearance = static_cast<float>(_settings.groundClearance);
			_clearance = StepClearance(_clearance, *ground + clearance - pose.position.y, clearance, _deltaTime);
			pose.position.y += _clearance;
		}
	}
	// The look up turns only the camera, about its own position.
	if (lookUp > 0.0f)
	{
		_appliedLookUp = lookUp;
		pose.rotation = Multiply(pose.rotation, LookUpRotation(lookUp));
	}

	// The land drag. The game held its grab point under the cursor for its own camera. Hold the grab point of the
	// user under the same cursor in the camera of the user. The result depends only on the grab point, the cursor
	// and this camera, so a change of the camera between frames cannot move the land.
	auto grab = _drag.Step(drag);
	if (grab && !_dragActive)
	{
		game.Debug(
		    std::format("camera mod: the land drag holds the point ({:.2f}, {:.2f}, {:.2f})", grab->x, grab->y, grab->z));
	}
	_dragActive = grab.has_value();
	_holdShift = {};
	std::optional<Ray> gameRay;
	const auto cursor = dragFields.holdCursor;
	if (grab)
	{
		auto ray = game.ScreenRay(cursor);
		if (!ray)
		{
			return false;
		}
		// The transform is still the camera of the game. Move the ray to the camera of the user.
		gameRay = *ray;
		auto shown = MoveRay(*ray, gamePose.position, gamePose.rotation, pose.position, pose.rotation);
		const auto pitchDrag = _lastMouseY.has_value();
		if (pitchDrag && dragFields.fade <= k_DragFadeThreshold)
		{
			// With the setting to zoom and turn about the hand, the game holds the land under the cursor while the
			// user holds the middle button. The vertical drag of that button is the pitch, not a land drag: take the
			// land under the cursor again in each frame, so the hold does not move the camera.
			grab = PointOnLevel(shown, grab->y);
			_drag.grab = grab;
		}
		else if (tiltChanged && inPlace && std::abs(viewTilt - previousViewTilt) > k_TiltTolerance)
		{
			// The user holds the land and pitches. Move the grab point by the move of the land under the cursor that
			// the pitch of this frame causes, so the hold follows the cursor but not the pitch.
			const auto before =
			    MoveRay(*ray, gamePose.position, gamePose.rotation, pose.position, inPlaceRotation(previousViewTilt));
			grab = *grab + (PointOnLevel(shown, grab->y) - PointOnLevel(before, grab->y));
			_drag.grab = grab;
		}
		_holdShift = HoldShift(shown, *grab);
		if (keepAboveLand)
		{
			// The clearance came from the land before the move. Do not let the camera go below the land after it.
			auto moved = pose.position + _holdShift;
			auto ground = game.HighestGround(moved);
			if (ground && moved.y < *ground)
			{
				const auto raise = *ground - moved.y;
				pose.position.y += raise;
				_clearance += raise;
				shown.origin.y += raise;
				_holdShift = HoldShift(shown, *grab);
			}
		}
		pose.position = pose.position + _holdShift;
		pose.focus = pose.focus + _holdShift;
	}
	if (!game.SetCamera({.position = pose.position, .rotation = pose.rotation}))
	{
		game.ReportError("the mod could not move the camera transform");
		return false;
	}
	game.SetFocus(pose.focus);
	game.SetDirection(pose.direction);

	if (grab && !dragFields.inSubScene && game.Drag().ignoreHand == 0)
	{
		// The drag must not take the focus out of the map. The game moves the focus and the transform back into
		// the map.
		game.ClampFocus();
		const auto clampShift = game.Focus() - pose.focus;
		pose.position = pose.position + clampShift;
		pose.focus = pose.focus + clampShift;
	}
	// The camera of the game for the focus that the game gets: the game places its camera there in the next frame,
	// so the collision test of the game sees only the moves of the next frame.
	const auto gamePosition = gameEquivalent.position + (pose.focus - gameEquivalent.focus);
	_written = CameraPlacement {.position = pose.position, .rotation = pose.rotation};
	_gameCamera = CameraPlacement {.position = gamePosition, .rotation = gameEquivalent.rotation};
	_shown = pose.position;
	_anchor.reset();
	_viewFocus.reset();
	if (tiltChanged && inPlace)
	{
		_anchor = pose.position - WorldPoint {0.0f, _clearance, 0.0f};
		_viewFocus =
		    InPlaceViewFocus(*_anchor, pose.focus.y, pose.direction, viewTilt, InPlaceFocusReach(stageHeight, gameTilt));
	}
	_trace.applied = true;
	_trace.appliedLookUp = _appliedLookUp;
	_trace.clearance = _clearance;
	_trace.holdShift = _holdShift;
	_trace.dragActive = _dragActive;
	_trace.anchor = _anchor;
	_trace.viewFocus = _viewFocus;
	_trace.writtenFocus = pose.focus;
	_trace.writtenCamera = _written;

	if (grab && gameRay)
	{
		// Give the game the land point under the cursor for its own camera in the next frame. Its own drag then
		// moves the focus almost as the loop does, so its collision test sees only a small move.
		const auto next = PointOnLevel(
		    MoveRay(*gameRay, gamePose.position, gamePose.rotation, gamePosition, gameEquivalent.rotation), grab->y);
		game.SetDragGrab(next);
		_drag.gameGrabWritten = next;
	}
	else if (!drag.holding)
	{
		// The game takes the land under the cursor when its next hold starts. Keep the same land point in the view
		// of the user.
		if (auto land = game.LandUnderScreenPoint(cursor))
		{
			_drag.idlePoint = land;
		}
	}
	return true;
}

void CameraLoop::StopGlide()
{
	_glide.Stop();
	_glideFocus.reset();
	_tracking = false;
	_dragVelocity.Reset();
}

void CameraLoop::StepGlide(CameraGame& game)
{
	_glideFocus.reset();
	if (_settings.dragInertia <= 0.0)
	{
		StopGlide();
		return;
	}
	auto held = game.MouseButton(k_RightMouseButton);
	if (!held)
	{
		StopGlide();
		return;
	}
	const auto height = std::max(game.StageHeight(), 1.0f);
	const auto stopSpeed = k_GlideStopHeights * height;
	if (*held)
	{
		// A new press stops the glide. The game takes a new grab point in this frame.
		_glide.Stop();
		_handCursor.reset();
		return;
	}
	if (std::exchange(_tracking, false))
	{
		// The user released the land in this frame. The glide continues the motion of the drag. The fade of the
		// game holds the land under its smoothed cursor and stops the motion, so the loop ends the fade here.
		// The camera that the user sees is behind the target by the lag of the chase, and it moves that distance
		// after the release too. So the glide is shorter by the lag, and the full move stays the speed times the
		// time constant.
		auto velocity = _dragVelocity.velocity;
		if (_settings.pitchMode == PitchMode::Hand && _settings.chaseSeconds > 0.0)
		{
			const auto corrected = velocity + _chase.lag * (1.0f / static_cast<float>(_settings.dragInertia));
			velocity = Dot(corrected, velocity) > 0.0f ? corrected : WorldPoint {};
		}
		if (_dragVelocity.HasVelocity() &&
		    _glide.Start(velocity, static_cast<float>(_settings.dragInertiaMaxSpeed) * height, stopSpeed))
		{
			game.SetDragFade(0.0f);
			_handCursor = game.Drag().handCursor;
			game.Debug(std::format("camera mod: the land glides at ({:.2f}, {:.2f}) metres for each second", _glide.velocity.x,
			                       _glide.velocity.z));
		}
		_dragVelocity.Reset();
	}
	if (!_glide.active)
	{
		return;
	}
	// Give the game the focus of the glide before its frame. The game then places its camera there, keeps it in the
	// map and above the land, and tests the move for collisions.
	const auto focus = game.Focus() + _glide.Step(_deltaTime, static_cast<float>(_settings.dragInertia), stopSpeed);
	game.SetFocus(focus);
	_glideFocus = focus;
	_trace.glideFocus = focus;
}

void CameraLoop::CheckGlide(CameraGame& game)
{
	if (!_glideFocus)
	{
		return;
	}
	// The glide stops at once if the game moved the focus itself: a key pan, an edge scroll, a zoom to the cursor,
	// the edge of the map or a collision. A new hold of the land also stops it.
	const auto focus = game.Focus();
	const auto moved = std::hypot(focus.x - _glideFocus->x, focus.z - _glideFocus->z) > k_GlideTolerance;
	if (moved || ReadDragFrame(game).holding)
	{
		_glide.Stop();
	}
	_glideFocus.reset();
}

void CameraLoop::TrackPosition(CameraGame& game, WorldPoint position)
{
	if (LandGripOn())
	{
		// The land drag of the hand rig: the user holds the land while the rig holds a grip.
		if (_settings.dragInertia <= 0.0 || !_grip)
		{
			_tracking = false;
			_dragVelocity.Reset();
			return;
		}
		if (!_tracking || _gripStarted)
		{
			_dragVelocity.Reset();
		}
		_dragVelocity.Add(position, _deltaTime, k_DragVelocitySeconds);
		_tracking = true;
		return;
	}
	const auto frame = ReadDragFrame(game);
	const auto fields = game.Drag();
	const auto buttonHold = _settings.dragInertia > 0.0 && frame.holding && fields.fade >= k_DragFadeHeld;
	if (!buttonHold)
	{
		_tracking = false;
		_dragVelocity.Reset();
		return;
	}
	if (!_tracking || frame.grabbedAgain || !SamePoint(fields.lastCursorTerrain, _dragBefore.lastCursorTerrain))
	{
		// A new hold, or a jump of the focus to the cursor: the speed starts again from this frame.
		_dragVelocity.Reset();
	}
	_dragVelocity.Add(position, _deltaTime, k_DragVelocitySeconds);
	_tracking = true;
}

void CameraLoop::EaseHand(CameraGame& game)
{
	if (!_handCursor)
	{
		return;
	}
	// While the user holds the land, the game moves the hand to the cursor with a delay. After the release, the
	// fade of the game no longer does this. The loop continues the same delay until the hand is at the cursor.
	auto cursor = game.InputPosition();
	if (!cursor || !game.Hand())
	{
		_handCursor.reset();
		return;
	}
	*_handCursor = *_handCursor + (*cursor - *_handCursor) * GameDragCursorStep(_deltaTime);
	if (Length(*cursor - *_handCursor) < k_HandEasePixels)
	{
		_handCursor.reset();
		return;
	}
	game.SetHandOverride(*_handCursor);
}

void CameraLoop::BlockPitchDragHold(CameraGame& game)
{
	if (!_settings.pitch && !_settings.lookUp)
	{
		return;
	}
	auto middle = game.MouseButton(k_MiddleMouseButton);
	const auto held = middle && *middle;
	_trace.modMiddle = middle;
	// The input of the game can see the release one frame after Unity does. Block the frame after the release too.
	const auto releasedNow = std::exchange(_middleHeld, held) && !held;
	const auto fields = game.Drag();
	if ((!held && !releasedNow) || fields.fade > k_DragFadeThreshold)
	{
		return;
	}
	// With the setting to zoom and turn about the hand, the game holds the land under the cursor while the user
	// holds the middle button, and while the height eases after a zoom. The vertical drag of that button is the
	// pitch: a hold would drag the land after the cursor and move the camera back. Stop the hold of the game for
	// this frame. The game does not test the UI for its hold, so the loop blocks it also when the cursor is over
	// the UI. The turn about the hand stays.
	if (fields.ignoreHand + fields.inhibitHand == 0)
	{
		// A count that is not zero already stops the hold. The loop does not make a count of the game shorter.
		game.SetHoldBlock(1);
	}
	_trace.holdBlocked = true;
}

void CameraLoop::RestoreGameCamera(CameraGame& game)
{
	const auto fields = game.Drag();
	_dragBefore = {.startedTime = fields.startedTime,
	               .holdingZoomRotate = fields.holdingZoomRotate,
	               .ignoreHand = fields.ignoreHand,
	               .inhibitHand = fields.inhibitHand,
	               .lastCursorTerrain = fields.lastCursorTerrain,
	               .pressPoint = std::nullopt};
	const auto written = std::exchange(_written, std::nullopt);
	const auto gameCamera = std::exchange(_gameCamera, std::nullopt);
	if (!written || !gameCamera)
	{
		return;
	}
	auto camera = game.Camera();
	if (!camera || !SamePlacement(*camera, *written))
	{
		// Other code moved the camera after the loop. Let the game start from that camera.
		_trace.movedByOther = camera.has_value();
		return;
	}
	// The game finds the land under the cursor with its own camera. The loop finds the land under the cursor in the
	// view of the user here, before it puts the camera of the game back.
	if (auto cursor = game.InputPosition())
	{
		_dragBefore.pressPoint = game.LandUnderScreenPoint(*cursor);
	}
	if (!game.SetCamera(*gameCamera))
	{
		game.ReportError("the mod could not put the camera of the game back");
		return;
	}
	_trace.restored = true;
}

DragFrame CameraLoop::ReadDragFrame(CameraGame& game) const
{
	// The game holds the land under the cursor while its drag fades out, and while the user turns or zooms if the
	// setting to zoom and turn about the hand is on. A count that stops the hand focus also stops the hold. A push
	// of the camera away from an object or from the land starts such a count.
	const auto fields = game.Drag();
	const auto blocked = fields.noHandFocus || _dragBefore.ignoreHand + _dragBefore.inhibitHand != 0;
	const auto pushed = _dragBefore.inhibitHand == 0 && fields.inhibitHand > 0;
	const auto active = fields.fade > k_DragFadeThreshold || fields.zoomRotateChanging;
	const auto dragStarted = fields.startedTime != _dragBefore.startedTime;
	const auto zoomRotatePressed = !_dragBefore.holdingZoomRotate && fields.holdingZoomRotate && !fields.dragValid;
	return {.holding = active && !blocked && !pushed,
	        .grabbedAgain = dragStarted || zoomRotatePressed,
	        .gameGrab = fields.grab,
	        .pressPoint = _dragBefore.pressPoint};
}

PitchLimits CameraLoop::PitchLimitsFromSettings() const
{
	return {.pitch = _settings.pitch,
	        .minimumTilt = static_cast<float>(_settings.minTiltDegrees),
	        .maximumTilt = static_cast<float>(_settings.maxTiltDegrees),
	        .lookUp = _settings.lookUp,
	        .lookUpLimits = {.maxAboveHorizon = static_cast<float>(_settings.maxLookUpDegrees),
	                         .fullStage = static_cast<float>(_settings.lookUpFullStage),
	                         .zeroStage = static_cast<float>(_settings.lookUpZeroStage)}};
}

void CameraLoop::ResetPitch()
{
	_pitchTarget = 0.0f;
	_pitchValue = 0.0f;
	_distanceScale = 1.0f;
	_clearance = 0.0f;
	_pitchFromInput = false;
	_pitchReset = false;
	_view = {};
	_lastMouseY.reset();
	_lastMouseX.reset();
	_lastPressMouse.reset();
	_written.reset();
	_gameCamera.reset();
	_drag = {};
	_dragActive = false;
	_holdShift = {};
	_anchor.reset();
	_viewFocus.reset();
	StopGlide();
	StopTurnGlides();
}

CameraLoop::PitchInput CameraLoop::ReadPitchInput(CameraGame& game)
{
	PitchInput result;
	if (!_settings.lookUp && !_settings.pitch && !_ground)
	{
		// In the ground range the middle button always turns the view up and down.
		return result;
	}
	auto held = game.MouseButton(k_MiddleMouseButton);
	auto mouse = game.MousePosition();
	if (!held || !mouse)
	{
		_lastMouseY.reset();
		_lastMouseX.reset();
		return result;
	}
	auto hand = game.Hand();
	const auto drag = *held && !(hand && hand->overUi);
	if (drag && _lastMouseY)
	{
		// The game uses only the horizontal part of a middle button drag (to turn). The vertical part changes the
		// pitch and the look up.
		result.vertical = mouse->y - *_lastMouseY;
		result.horizontal = _lastMouseX ? mouse->x - *_lastMouseX : 0.0f;
		result.raise =
		    result.vertical * static_cast<float>(_settings.pitchSensitivity) / 100.0f * (_settings.invertPitch ? -1.0f : 1.0f);
	}
	else if (drag && _settings.pitch && _settings.resetPitchOnDoubleClick)
	{
		// This is a new press of the button. A second press soon after the first, at the same place, is a double
		// click.
		if (_lastPressMouse && _seconds - _lastPress < k_DoubleClickSeconds &&
		    Length(*mouse - *_lastPressMouse) < k_DoubleClickPixels)
		{
			result.reset = true;
			_lastPressMouse.reset();
		}
		else
		{
			_lastPressMouse = *mouse;
			_lastPress = _seconds;
		}
	}
	_lastMouseY = drag ? std::optional<float>(mouse->y) : std::nullopt;
	_lastMouseX = drag ? std::optional<float>(mouse->x) : std::nullopt;
	return result;
}

std::optional<WorldPoint> CameraLoop::HandPivot(CameraGame& game)
{
	auto hand = game.Hand();
	if (!hand || !hand->visible || hand->overUi || IsZero(hand->point))
	{
		return std::nullopt;
	}
	if (_pitchValue > 0.0f)
	{
		// With a flatter view or the view turned up, the cursor can point at the sky. Then the hand point is not on
		// the land, so the turn keeps the last hand point that was on the land.
		auto onLand = game.CursorOnLand();
		if (!onLand || !*onLand)
		{
			return _lastLandPoint;
		}
	}
	_lastLandPoint = hand->point;
	return hand->point;
}

bool CameraLoop::LoadStageTable(CameraGame& game)
{
	auto table = game.StageTable();
	if (!table || table->size() < 2)
	{
		game.ReportError("the camera mod cannot read the zoom stage table of the game");
		return false;
	}
	_stageTable = std::move(*table);
	_flattestStageTilt = std::ranges::min(_stageTable, {}, &StageView::tilt).tilt;
	return true;
}

void CameraLoop::AddWheelToTarget(float wheel, StageLimits limits)
{
	if (_settings.continuousZoom && _stageTable.size() >= 2)
	{
		// Each notch changes the height of the target by the same ratio, so the zoom speed follows the height.
		_zoom.target = StageAfterNotches(_stageTable, std::clamp(_zoom.target, limits.minimum, limits.maximum),
		                                 wheel / k_WheelAxisPerNotch, static_cast<float>(_settings.zoomStepRatio), limits,
		                                 ZoomGroundHeight());
		return;
	}
	_zoom.AddToTarget(WheelStageChange(wheel, static_cast<float>(_settings.wheelZoomStep)), limits);
}

float CameraLoop::StepZoom(StageLimits limits, bool intoLimits)
{
	const auto seconds = _settings.smoothZoom ? static_cast<float>(_settings.zoomSmoothing) : 0.0f;
	if (!_settings.continuousZoom || _stageTable.size() < 2)
	{
		return intoLimits ? _zoom.StepIntoLimits(_deltaTime, seconds, limits) : _zoom.Step(_deltaTime, seconds, limits);
	}
	// The value moves to the target in the logarithm of the height: the same part of the remaining ratio in each frame.
	_zoom.target = std::clamp(_zoom.target, limits.minimum, limits.maximum);
	_zoom.value = StepStageInHeight(_stageTable, _zoom.value, _zoom.target, _deltaTime, seconds, ZoomGroundHeight());
	if (!intoLimits)
	{
		_zoom.value = std::clamp(_zoom.value, limits.minimum, limits.maximum);
	}
	return _zoom.value;
}

void CameraLoop::ScalePan(CameraGame& game)
{
	const auto groundPart = _settings.pitchMode == PitchMode::Hand ? GroundPart(_zoom.value) : 0.0f;
	if (_settings.panSpeed <= 0.0 && groundPart <= 0.0f)
	{
		return;
	}
	// The game multiplies its key pan by a factor of the height of the camera that stops at 1 and at 10, and by its
	// move speed setting. The loop changes the setting for this frame, so that the key pan moves a fixed part of the
	// view width in each second at each height. The game keeps its own smoothing of the keys.
	auto camera = game.Camera();
	auto speed = game.MoveSpeed();
	auto fieldOfView = game.FieldOfView();
	auto screen = game.ScreenSize();
	if (!camera || !speed || !fieldOfView || !screen || screen->y <= 0.0f)
	{
		return;
	}
	if (!_gameMoveSpeed || *speed != _writtenMoveSpeed)
	{
		// Keep the value of the game. If a frame stopped before `EndFrame`, the value is still the value of the loop,
		// and the loop puts back the value that it kept.
		_gameMoveSpeed = *speed;
	}
	// The focus of the hand rig belongs to the target camera. The camera that the user sees is behind it in a chase.
	const auto distance =
	    Length((_rig && _settings.pitchMode == PitchMode::Hand ? TargetCamera() : camera->position) - game.Focus());
	const auto aspect = screen->x / screen->y;
	auto scale =
	    PanSpeedScale(static_cast<float>(_settings.panSpeed), ViewWidth(distance, *fieldOfView, aspect), camera->position.y);
	if (groundPart > 0.0f && _stageTable.size() >= 2)
	{
		// In the ground range the focus of the game can be far away. The speed goes from the speed at the closest zoom
		// stage to the walk speed on the land.
		const auto heightFactor = k_GameKeyPanSpeed * GamePanHeightFactor(camera->position.y);
		const auto detail = StageDistance(_stageTable.front().height, _stageTable.front().tilt);
		const auto top = _settings.panSpeed > 0.0
		                     ? static_cast<float>(_settings.panSpeed) * ViewWidth(detail, *fieldOfView, aspect)
		                     : heightFactor;
		scale = GroundPanSpeed(top, static_cast<float>(_settings.groundWalkSpeed), groundPart) / heightFactor;
	}
	const auto value = *_gameMoveSpeed * scale;
	if (game.SetMoveSpeed(value))
	{
		_writtenMoveSpeed = value;
		_moveSpeedToRestore = *_gameMoveSpeed;
		_trace.panScale = scale;
	}
}

void CameraLoop::RestoreMoveSpeed(CameraGame& game)
{
	if (!_moveSpeedToRestore)
	{
		return;
	}
	const auto speed = *_moveSpeedToRestore;
	_moveSpeedToRestore.reset();
	if (!game.SetMoveSpeed(speed))
	{
		game.ReportError("the mod could not put back the move speed of the town camera");
	}
}

void CameraLoop::SmoothKeyZoom(CameraGame& game)
{
	if (!_settings.continuousZoom)
	{
		return;
	}
	// The game zooms with the keys by whole stages, unless its setting for a smooth key zoom is on. The loop sets it
	// for this frame, and `EndFrame` puts the setting of the game back.
	auto smooth = game.DollySmooth();
	if (!smooth)
	{
		return;
	}
	if (!_gameDollySmooth || !*smooth)
	{
		// Keep the value of the game. If a frame stopped before `EndFrame`, the value is still the value of the loop,
		// and the loop puts back the value that it kept.
		_gameDollySmooth = *smooth;
	}
	if (*_gameDollySmooth)
	{
		return;
	}
	if (game.SetDollySmooth(true))
	{
		_dollyToRestore = false;
	}
}

void CameraLoop::RestoreDollySmooth(CameraGame& game)
{
	if (!_dollyToRestore)
	{
		return;
	}
	const auto smooth = *_dollyToRestore;
	_dollyToRestore.reset();
	if (!game.SetDollySmooth(smooth))
	{
		game.ReportError("the mod could not put back the key zoom setting of the town camera");
	}
}

void CameraLoop::RemoveEdgeScroll(CameraGame& game)
{
	if (_settings.edgeScroll || !_startRotation)
	{
		return;
	}
	auto edge = game.EdgeScroll();
	auto screen = game.ScreenSize();
	auto hand = game.Hand();
	if (!edge || !edge->active || !screen || (hand && hand->overUi))
	{
		// Over the UI the game does not scroll.
		return;
	}
	// The game added its edge scroll to the pan of the frame. The loop finds the same move with the rule of the game and
	// moves the focus and the camera back by it.
	const auto move = EdgeScrollMove(EdgeScrollDirection(edge->cursor, *screen), edge->speed, edge->deltaTime, *_startRotation);
	if (Length(move) <= 0.0f)
	{
		return;
	}
	game.SetFocus(game.Focus() - move);
	if (auto camera = game.Camera())
	{
		static_cast<void>(game.SetCamera({.position = camera->position - move, .rotation = camera->rotation}));
	}
	_trace.edgeScrollRemoved = move;
}

void CameraLoop::StopTurnGlides()
{
	_turnGlide.Stop();
	_pitchGlide.Stop();
	_turnRate.Reset();
	_pitchRate.Reset();
	_turnTracking = false;
	_pitchTracking = false;
}

nlohmann::json CameraLoop::State() const
{
	return {
	    {"zoomTarget", _zoom.target},
	    {"zoomValue", _zoom.value},
	    {"headingTarget", _heading.target},
	    {"headingValue", _heading.value},
	    {"lastWheel", _lastWheel},
	    {"pitchOffset", _pitchValue},
	    {"pitchTarget", _pitchTarget},
	    {"pitchRange", {_range.steepest, _range.flattest, _range.highest}},
	    {"tilt", _view.tilt},
	    {"lookUp", _view.lookUp},
	    {"maxLookUp", _range.highest - _range.flattest},
	    {"distanceScale", _distanceScale},
	    {"clearanceCorrection", _clearance},
	    {"lastLandPoint", ToJson(_lastLandPoint)},
	    {"dragActive", _dragActive},
	    {"grabPoint", ToJson(_drag.grab)},
	    {"holdShift", ToJson(_holdShift)},
	    {"dragIdlePoint", ToJson(_drag.idlePoint)},
	    {"restoresGameCamera", _gameCamera.has_value()},
	    {"dragVelocity", ToJson(_glide.active ? _glide.velocity : _dragVelocity.velocity)},
	    {"inertiaActive", _glide.active},
	    {"pitchMode", PitchModeName(_settings.pitchMode)},
	    {"appliedLookUp", _appliedLookUp},
	    {"cameraPosition", ToJson(_shown)},
	    {"pitchAnchor", ToJson(_anchor)},
	    {"viewFocus", ToJson(_viewFocus)},
	    {"rigPosition", _rig ? ToJson(_rig->position) : nlohmann::json()},
	    {"rigYaw", _rig ? nlohmann::json(_rig->yaw) : nlohmann::json()},
	    {"rigPitch", _rig ? nlohmann::json(_rig->pitch) : nlohmann::json()},
	    {"rigPivot", ToJson(_pivot)},
	    {"rigPivotLimited", _pivotLimited},
	    {"rigLevel", _level},
	    {"cursorLock", ToJson(_cursorLock)},
	    {"armPivot", ToJson(_armPivot)},
	    {"armLength", _armLength},
	    {"armLift", Length(_armPush)},
	    {"armPush", ToJson(_armPush)},
	    {"turnGlide", _turnGlide.active ? _turnGlide.rate : 0.0f},
	    {"pitchGlide", _pitchGlide.active ? _pitchGlide.rate : 0.0f},
	    {"zoomHeight", _stageTable.size() >= 2 ? nlohmann::json(HeightOfStage(_stageTable, _zoom.value)) : nlohmann::json()},
	    {"groundBlend", _ground ? nlohmann::json(GroundBlend(GroundPart(_zoom.value))) : nlohmann::json()},
	    {"groundLand", _ground ? nlohmann::json(_ground->land) : nlohmann::json()},
	    {"eyeHeight", _ground && _rig ? nlohmann::json(_rig->position.y - _ground->land) : nlohmann::json()},
	    {"chaseLag", ToJson(_chase.lag)},
	    {"chaseVelocity", ToJson(_chase.velocity)},
	    {"gripPoint", _grip ? ToJson(_grip->point) : nlohmann::json()},
	    {"gripApplied", _grip ? ToJson(_grip->applied) : nlohmann::json()},
	    {"turnInputPending", _turnInput.pending},
	    {"pitchInputPending", _pitchInput.pending},
	};
}

} // namespace moa_mod::camera
