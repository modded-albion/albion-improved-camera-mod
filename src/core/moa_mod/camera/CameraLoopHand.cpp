/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <cmath>

#include <algorithm>
#include <format>
#include <limits>
#include <utility>

#include <moa_mod/camera/CameraLoop.h>

namespace moa_mod::camera
{

namespace
{
constexpr int32_t k_LeftMouseButton = 0;
/// The identifier of an object that the sphere test touched when the game does not tell which object.
constexpr uint64_t k_UnknownObject = ~uint64_t {0};
constexpr int32_t k_MiddleMouseButton = 2;
constexpr int32_t k_RightMouseButton = 1;
/// The game holds the land under the cursor while the fade of its drag is above this value.
constexpr float k_DragFadeThreshold = 0.01f;
/// The largest change of a camera position or rotation that the rig accepts as its own camera.
constexpr float k_PlacementTolerance = 1e-3f;
constexpr float k_TurnTolerance = 1e-4f;
constexpr float k_StageTolerance = 1e-4f;
/// A pitch change of a frame below this angle, degrees, is not a gesture.
constexpr float k_PitchGestureDegrees = 1e-3f;
/// A zoom change of a frame below this part of a stage is not a gesture.
constexpr float k_ZoomGestureStages = 1e-5f;
constexpr float k_LookUpEaseSeconds = 0.4f;
/// With the turn lock, a frame of a middle button drag does not turn when its horizontal move is less than this
/// part of its vertical move.
constexpr float k_TurnLockRatio = 0.5f;
/// The count of halvings that find the largest part of a pitch that keeps the camera above the land.
constexpr int k_PitchLimitSteps = 12;
/// A difference between the camera of the game and its state that is larger than this is not a camera shake, metres.
constexpr float k_ShakeMetres = 5.0f;
/// A turn between the camera of the game and its state that is larger than this is not a camera shake, degrees.
constexpr float k_ShakeDegrees = 10.0f;
/// The smallest height of a zoom stage that the rig uses to divide, metres.
constexpr float k_MinimumStageHeight = 0.1f;
/// The smallest tilt of a zoom stage that the rig uses to divide, degrees.
constexpr float k_FlattestZoomTilt = 1.0f;
/// The smallest sine of the angle of the line to the pivot below the horizon that a zoom uses.
constexpr float k_SteepestPivotSine = 0.05f;
/// A zoom of one stage keeps `exp(-value)` of the difference between the distance of the camera to the pivot and
/// the distance of the game camera of the stage, as a ratio.
constexpr float k_ZoomExcessDecay = 2.0f;
/// The smallest ratio of the distance to the pivot to the distance of the game camera that a zoom uses.
constexpr float k_MinimumZoomExcess = 0.01f;
/// At a press, the rig takes the hand as the pivot if the hand is at most this distance from the cursor, pixels.
constexpr float k_HandPressPixels = 100.0f;
/// A move of the game smaller than this does not move the pivot on the screen, metres.
constexpr float k_GameShiftTolerance = 1e-3f;
/// The radius of the sphere that the spring arm moves from the pivot to the camera: the radius of the collision test
/// of the camera of the game, metres.
constexpr float k_ArmRadius = 0.6f;
/// The sphere of the spring arm starts this distance from the pivot, metres. The pivot is on the land.
constexpr float k_ArmCastStart = 0.25f;
/// A hit nearer than this to the start of the sphere is an object that the sphere started in, metres.
constexpr float k_ArmOverlap = 1e-3f;
/// A hit nearer than this to the pivot is an object that the pivot is on or in, metres. The sphere starts again
/// after it.
constexpr float k_ArmPivotContact = 2.0f;
/// After a start in an object, or a hit near the pivot, the sphere starts again this distance farther out, metres.
constexpr float k_ArmCastRetry = 2.0f;
/// The pivot is on the land, so the line from the pivot can touch the land near the pivot. Before the line is clear of
/// the land, a point under the land stops the arm only if it is deeper than this, metres.
constexpr float k_ArmLandContactDepth = 2.0f;
/// The land samples of the spring arm are this distance apart at the pivot, metres, and farther apart by this part of
/// the distance from the pivot.
constexpr float k_ArmFirstLandStep = 0.5f;
constexpr float k_ArmLandStepGrowth = 0.02f;
/// The land can rise by at most this height for each metre along the arm between two samples. Where the line is less
/// high above the land at the two samples than this permits, the arm examines the land between them too.
constexpr float k_ArmRidgeSlope = 4.0f;
/// The examination between two samples stops at this distance between the samples (the distance between two points
/// of the height grid of the land), metres, or after this count of samples in a frame.
constexpr float k_ArmRidgeStep = 0.25f;
constexpr int k_ArmRidgeSamples = 64;
/// The camera ball does its pushes again at most this count of times in a frame.
constexpr int k_LandBallPasses = 3;
/// The push of the land never moves the camera faster than this, metres for each second. The floor above the land can.
constexpr float k_LandPushSpeed = 30.0f;
/// The spring arm stops its ease when it is nearer than this to its target, metres.
constexpr float k_ArmSnap = 1e-3f;
/// The halvings that find the arm length of a raised camera stop at this precision or at this count, metres.
constexpr float k_ArmLiftPrecision = k_ArmSnap * 0.25f;
constexpr int k_ArmLiftSteps = 32;
/// The count of tries to give the game a focus in the map with the camera where it is.
constexpr int k_ClampAttempts = 4;
/// The smallest distance of a nearer focus on the line of the heading, metres.
constexpr float k_SmallestClampReach = 1.0f;
/// The largest change of the desired distance of the spring arm that the loop accepts in a frame with no zoom and no
/// new pivot, as a part of the distance.
constexpr float k_DesiredTolerance = 1e-4f;
/// The rig must be at least this far from the pivot: the larger of the distance in metres and the part of the
/// distance of the camera of the game at the zoom stage. A rig that is nearer goes back to the distance of the stage.
constexpr float k_SmallestDesiredMetres = 1.0f;
constexpr float k_SmallestDesiredPart = 0.1f;
/// The spring arm is never shorter than the larger of this distance in metres and this part of the distance of the
/// camera of the game at the zoom stage (or the desired distance, if that is shorter).
constexpr float k_ShortestArmMetres = 2.0f;
constexpr float k_ShortestArmPart = 0.1f;
/// The land pulls the arm in at most by this part of the desired distance for each second.
constexpr float k_ArmPullDistances = 0.5f;
/// The dead band of the reach of a pushed camera beyond the desired distance, in which the arm keeps its length, metres.
constexpr float k_ArmPushBand = 0.05f;
/// A clamp into the map longer than this goes through the chase, metres.
constexpr float k_ClampChaseMetres = 1.0f;
/// The arm goes back out at most by this part of the desired distance for each second.
constexpr float k_ArmReturnDistances = 0.6f;
/// The age, in seconds, at which a frame of a turn or a pitch has approximately 37 percent of its weight in the rate.
constexpr float k_GlideRateSeconds = 0.1f;
/// The glide after a turn or a pitch stops when its rate is this value or less, degrees for each second.
constexpr float k_GlideStopDegrees = 0.5f;

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

/// The angle of a rotation, degrees.
[[nodiscard]] float AngleOf(Quaternion q)
{
	return 2.0f * std::acos(std::min(std::abs(q.w), 1.0f)) / k_DegreesToRadians;
}
} // namespace

float CameraLoop::StageHeightOf(float stage) const
{
	return std::max(InterpolateStage(_stageTable, stage).height, k_MinimumStageHeight);
}

float CameraLoop::StageTiltOf(float stage) const
{
	return InterpolateStage(_stageTable, stage).tilt;
}

bool CameraLoop::ResyncHand(CameraGame& game)
{
	// A start from the camera of the game is not a move of the user: the clamp moves the camera at once.
	_resyncing = true;
	struct ResyncEnd
	{
		bool& flag;
		~ResyncEnd() { flag = false; }
	} resyncEnd {_resyncing};
	if (!LoadStageTable(game))
	{
		return false;
	}
	auto camera = game.Camera();
	if (!camera)
	{
		return false;
	}
	ResetPitch();
	ResetMotion();
	// The return from a stand-back. If the camera is not where the mod put it, the game moved it (a blend, a cutscene).
	// The camera of the game can then have a tilt that is not the view of the user, so the rig starts from the state
	// of the game, not from the transform.
	const auto handBack = std::exchange(_handBack, false);
	const auto standBackCamera = std::exchange(_standBackCamera, std::nullopt);
	const auto standBackSpot = std::exchange(_standBackSpot, std::nullopt);
	const auto movedByGame = handBack && !(standBackCamera && SamePlacement(*camera, *standBackCamera));
	if (ResumeGround(*camera))
	{
		ResetArm();
		_pivot.reset();
		_pivotLimited = false;
		_flight.reset();
		_rigMiddleHeld = false;
		if (!WriteHandState(game, {}, {}))
		{
			return false;
		}
		_resync = false;
		return true;
	}
	_ground.reset();
	const auto stage = game.Stage();
	if (movedByGame)
	{
		ResyncFromGameState(game);
		Tell(CameraEventKind::StandBack, "hand-back from the state of the game");
		if (_settings.flyBackAfterStandBack && standBackSpot)
		{
			// The camera flies back to the view that the user had before the stand-back.
			_flightRequest = FlightRequest {.kind = FlightRequest::Kind::Spot, .spot = *standBackSpot, .northDegrees = 0.0f};
		}
	}
	else
	{
		const auto pitch = PitchOf(camera->rotation);
		_rig = HandRigPose {.position = camera->position, .yaw = game.Direction(), .pitch = pitch};
		// Start from the tilt of the camera of the game. The raise is the difference from the tilt of the stage row.
		_pitchTarget = StageTiltOf(stage) - pitch;
	}
	_heading.Reset(_rig->yaw);
	_zoom.Reset(stage);
	_pitchValue = _pitchTarget;
	_lastStage = stage;
	_pivot.reset();
	_pivotLimited = false;
	_flight.reset();
	_level = game.Focus().y;
	_rigMiddleHeld = false;
	ResetArm();
	if (!WriteHandState(game, {}, {}))
	{
		return false;
	}
	_resync = false;
	return true;
}

void CameraLoop::ResyncFromGameState(CameraGame& game)
{
	// The camera of the game for its focus, heading and stage, with the tilt of the stage row: the view of the game with
	// no raise of the user.
	const auto stage = game.Stage();
	const auto direction = game.Direction();
	const auto tilt = StageTiltOf(stage);
	_rig = HandRigPose {
	    .position = game.Focus() + GameCameraOffset(direction, StageHeightOf(stage), tilt), .yaw = direction, .pitch = tilt};
	_pitchTarget = 0.0f;
}

bool CameraLoop::BeforeHand(CameraGame& game)
{
	auto camera = game.Camera();
	if (!camera)
	{
		_resync = true;
		return false;
	}
	if (!_resync && _written && !SamePlacement(*camera, *_written))
	{
		// Other code moved the camera after the rig. Start again from that camera.
		_trace.movedByOther = true;
		_resync = true;
	}
	if (_resync && !ResyncHand(game))
	{
		_resync = true;
		return false;
	}
	_startDirection = game.Direction();
	if (auto middle = game.MouseButton(k_MiddleMouseButton); middle && *middle && !_rigMiddleHeld)
	{
		// A press of the middle button takes the land for a turn and a pitch. The glide and the fade of the last land
		// drag stop, so that they do not move the camera away from the pivot during the hold.
		StopGlide();
		if (game.Drag().fade > 0.0f)
		{
			game.SetDragFade(0.0f);
		}
	}
	BlockGameHold(game);
	if (_settings.springArm || _ground || _zoom.target < 0.0f)
	{
		// The ground range needs it too: the push of the game keeps its camera 3.5 metres above the land.
		StopGameCollisions(game);
	}
	const auto fields = game.Drag();
	_dragBefore = {.startedTime = fields.startedTime,
	               .holdingZoomRotate = fields.holdingZoomRotate,
	               .ignoreHand = fields.ignoreHand,
	               .inhibitHand = fields.inhibitHand,
	               .lastCursorTerrain = fields.lastCursorTerrain,
	               .pressPoint = std::nullopt};
	_lastCursorTerrainBefore = fields.lastCursorTerrain;
	// The double click of the game on the land starts a zoom to a stage in the frame of the game.
	_zoomToStageBefore = game.ZoomToStage();
	if (!_focusBox)
	{
		_focusBox = game.FocusLimits();
	}

	// The game must not ease its height, tilt and focus height in this frame. Then the game places its camera exactly
	// at the rig camera, and its land drag, push, clamp and collision test act on the camera that the user sees.
	auto eases = game.Eases();
	if (!eases || !game.SetEases({.stage = 0.0f, .focusHeight = 0.0f}))
	{
		game.ReportError("the hand rig cannot stop the eases of the town camera");
		_resync = true;
		return false;
	}
	if (!_gameEases || eases->stage != 0.0f || eases->focusHeight != 0.0f)
	{
		// Keep the values of the game. If a frame stopped before `EndFrame`, the eases are still zero, and the loop
		// puts back the values that it kept.
		_gameEases = *eases;
	}
	_easesToRestore = *_gameEases;
	StepGlide(game);

	auto limits = ZoomLimits(game);
	if (!limits)
	{
		_resync = true;
		return false;
	}
	auto hand = game.Hand();
	_lastWheel = 0.0f;
	_wheelNotch = false;
	if (!(hand && hand->overUi))
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
		// The rig uses this wheel step. The game must not use it too, so the game ignores the wheel in this frame.
		// `EndFrame` puts the old value back.
		auto previous = game.IgnoreWheel();
		if (previous && game.SetIgnoreWheel(true))
		{
			_wheelToRestore = *previous;
			AddWheelToTarget(_lastWheel, RigZoomLimits(*limits));
			_wheelNotch = true;
		}
	}
	_trace.handRig = true;
	_trace.writtenStage = _stageWritten;
	_trace.zoomTarget = _zoom.target;
	_frameActive = true;
	return true;
}

void CameraLoop::BlockGameHold(CameraGame& game)
{
	// The game holds the land under the cursor in two cases: the land drag of the right button, and (with the
	// setting to zoom and turn about the hand) a zoom or a turn. The rig does the zoom and the turn about the hand
	// itself, so it stops the second hold. With the land grip, the rig also does the land drag, so it stops the first
	// hold too. Else the land drag stays the drag of the game.
	auto right = game.MouseButton(k_RightMouseButton);
	const auto fields = game.Drag();
	if (!LandGripOn() && ((right && *right) || fields.fade > k_DragFadeThreshold))
	{
		return;
	}
	if (fields.ignoreHand + fields.inhibitHand == 0)
	{
		// A count that is not zero already stops the hold. The rig does not make a count of the game shorter.
		game.SetHoldBlock(1);
	}
	_trace.holdBlocked = true;
}

void CameraLoop::RestoreEases(CameraGame& game)
{
	if (!_easesToRestore)
	{
		return;
	}
	const auto eases = *_easesToRestore;
	_easesToRestore.reset();
	if (!game.SetEases(eases))
	{
		game.ReportError("the mod could not put back the eases of the town camera");
	}
}

void CameraLoop::StopGameCollisions(CameraGame& game)
{
	// The spring arm keeps the camera out of the land (and the objects, if that is on). The push and the collision test of the
	// game move the camera away from the pivot, and the rig takes each move of the game. So the game does not do them in this
	// frame, and `EndFrame` puts the setting of the game back.
	const auto ignores = game.IgnoresCollisions();
	if (!_gameIgnoresCollisions || !ignores)
	{
		// Keep the value of the game. If a frame stopped before `EndFrame`, the value is still the value of the loop,
		// and the loop puts back the value that it kept.
		_gameIgnoresCollisions = ignores;
	}
	if (*_gameIgnoresCollisions)
	{
		// The game already does no push and no collision test.
		return;
	}
	if (game.SetIgnoresCollisions(true))
	{
		_collisionsToRestore = false;
		_trace.gamePushIgnored = true;
	}
	else if (!_groundCollisionsFailed)
	{
		// Without this setting the game pushes its camera above the land, so the camera cannot go near the land.
		_groundCollisionsFailed = true;
		game.Debug("camera mod: the game does not let the mod stop its land push; the ground look is off");
	}
}

void CameraLoop::RestoreGameCollisions(CameraGame& game)
{
	if (!_collisionsToRestore)
	{
		return;
	}
	const auto ignores = *_collisionsToRestore;
	_collisionsToRestore.reset();
	if (!game.SetIgnoresCollisions(ignores))
	{
		game.ReportError("the mod could not put back the collision setting of the town camera");
	}
}

std::optional<WorldPoint> CameraLoop::LandUnderCursor(CameraGame& game, std::optional<WorldPoint> screen)
{
	auto hand = game.Hand();
	if (hand && hand->overUi)
	{
		return std::nullopt;
	}
	auto cursor = screen ? screen : game.InputPosition();
	if (!cursor)
	{
		return std::nullopt;
	}
	auto land = game.LandUnderScreenPoint(*cursor);
	if (land)
	{
		_lastLandPoint = land;
	}
	return land;
}

bool CameraLoop::AfterHand(CameraGame& game)
{
	if (!_rig)
	{
		return false;
	}
	CheckGlide(game);
	_groundExit = false;
	_frameInput = {};
	auto camera = game.Camera();
	auto limits = ZoomLimits(game);
	if (!camera || !limits)
	{
		return false;
	}
	const auto focus = game.Focus();
	const auto direction = game.Direction();
	const auto stage = game.Stage();
	const StageView gameView {.height = game.StageHeight(), .tilt = game.StageTilt()};
	const auto fields = game.Drag();
	auto& rig = *_rig;
	_trace.pivotKept = true;
	const auto arm = _settings.springArm;

	// The game moved its focus by the key pan, the edge scroll, the land drag, the glide and the clamp into the map.
	// With no spring arm, the push above the land and the collision test of the game move the focus too. The camera
	// of the game moved with its focus. The rig takes each of these moves as the truth: it does not move the camera
	// back.
	auto shift = focus - _focusWritten;
	if (!SamePoint(fields.lastCursorTerrain, _lastCursorTerrainBefore))
	{
		// The game moved its focus to the land under the cursor for a zoom with the keys. The rig zooms to the
		// hand, so it does not take that jump.
		shift = {};
	}
	// In the ground range the rig is the camera that the user saw. The camera moves from there, and the land (and, with
	// the object collision on, the objects) stops it (`StepGround`).
	const auto groundStart = rig.position;
	shift = _ground ? GuardGroundDrag(game, shift) : SoftenGameShift(game, shift);
	rig.position = rig.position + shift;
	if (_armPivot)
	{
		if (_pivot && SamePoint(*_pivot, *_armPivot))
		{
			// The pivot of the gesture moves with the camera, as the pivot of the spring arm does. Thus the arm keeps
			// its pivot and its desired distance, and the pivot stays at its place on the screen.
			_pivot = *_pivot + shift;
		}
		_armPivot = *_armPivot + shift;
	}
	_trace.gameShift = shift;
	if (Length(shift) > k_GameShiftTolerance)
	{
		// The game moved the camera (the key pan, the land drag, the clamp or the push), so the pivot moved on the
		// screen.
		_trace.pivotKept = false;
		// The spring arm moved with the camera. The rig keeps its desired distance: a camera that an obstacle pulled
		// in must not give the rig a shorter distance.
	}

	// The camera shake of the game: the part of the camera of the game that does not come from its state.
	auto shake = camera->position - (focus + GameCameraOffset(direction, gameView.height, gameView.tilt));
	auto shakeTurn = Multiply(camera->rotation, Inverse(HandRigRotation(direction, gameView.tilt)));
	if (Length(shake) > k_ShakeMetres || AngleOf(shakeTurn) > k_ShakeDegrees)
	{
		shake = {};
		shakeTurn = {};
	}

	// The input of this frame: the turn of the game, the zoom target and the vertical drag of the middle button.
	const auto input = ReadPitchInput(game);
	auto middle = game.MouseButton(k_MiddleMouseButton);
	const auto middleHeld = middle && *middle;
	_trace.modMiddle = middle;
	const auto pressed = middleHeld && !_rigMiddleHeld;
	_rigMiddleHeld = middleHeld;
	auto right = game.MouseButton(k_RightMouseButton);
	const auto rightHeld = right && *right;
	const auto rightPressed = rightHeld && !_rigRightHeld;
	if (rightPressed)
	{
		// The land drag of the game moves the camera from now on. The gesture ends, and the next gesture takes a new
		// pivot.
		_pivot.reset();
	}
	_rigRightHeld = rightHeld;
	_frameInput = {.middle = middleHeld, .right = rightHeld, .pressed = pressed};

	// The game keeps its stage in its zoom limits in each frame. For example, while the hand holds an object, the
	// smallest stage is not zero. A move of the stage into the limits is not a zoom of the keys.
	const auto expectedStage = std::clamp(_stageWritten, limits->minimum, limits->maximum);
	const auto keyZoom = std::abs(stage - expectedStage) > k_StageTolerance;
	if (keyZoom)
	{
		// The keys of the game changed the stage. The rig zooms to the new stage smoothly, about the hand.
		if (_settings.continuousZoom)
		{
			// The target changes its height by the same ratio as the game changed the height of its stage.
			const auto ratio = StageHeightOf(stage) / StageHeightOf(expectedStage);
			const auto groundHeight = ZoomGroundHeight();
			const auto rigLimits = RigZoomLimits(*limits);
			_zoom.target = std::clamp(
			    StageOfHeight(_stageTable, HeightOfStage(_stageTable, _zoom.target, groundHeight) * ratio, groundHeight),
			    rigLimits.minimum, rigLimits.maximum);
		}
		else
		{
			_zoom.AddToTarget(stage - expectedStage, *limits);
		}
	}

	// The navigation. Any input of the user stops a flight, and the view stays where the flight put it. Then a double
	// click of the game on the land, a camera spot or the reset of the view starts a flight.
	auto left = game.MouseButton(k_LeftMouseButton);
	const auto leftHeld = left && *left;
	const auto leftPressed = leftHeld && !_rigLeftHeld;
	_rigLeftHeld = leftHeld;
	if (_flight)
	{
		const auto turnedByGame = std::abs(WrapDegrees(direction - _startDirection)) > k_TurnTolerance;
		if (pressed || rightHeld || leftPressed || _wheelNotch || keyZoom || turnedByGame || input.reset ||
		    Length(shift) > k_GameShiftTolerance)
		{
			_flight.reset();
			_trace.flightStopped = true;
		}
	}
	if (!StartFlights(game, leftPressed, *limits))
	{
		return false;
	}
	if (_flight)
	{
		// The flight owns the rig in this frame: no gesture, no zoom and no pitch of the user. The flight has its own
		// ease, so the chase only lets the lag of the camera before the flight go.
		if (!StepFlight(game, !game.IgnoresCollisions()))
		{
			return false;
		}
		StepChase(game, {});
		if (!WriteHandState(game, shake, shakeTurn))
		{
			return false;
		}
		LockCursor(game, middleHeld, pressed);
		TrackPosition(game, TargetCamera());
		if (!_cursorLock)
		{
			EaseHand(game);
		}
		_trace.applied = true;
		return true;
	}

	// The land drag of the rig moves the target camera, and the pivots with it, as a move of the game does.
	WorldPoint gripMove;
	if (!StepGrip(game, rightHeld, rightPressed, gripMove))
	{
		return false;
	}
	if (Length(gripMove) > 0.0f)
	{
		rig.position = rig.position + gripMove;
		if (_armPivot)
		{
			if (_pivot && SamePoint(*_pivot, *_armPivot))
			{
				_pivot = *_pivot + gripMove;
			}
			_armPivot = *_armPivot + gripMove;
		}
		_trace.pivotKept = false;
	}
	if (!_ground && Length(shift + gripMove) > 0.0f && !KeepArmPivotOnLand(game))
	{
		return false;
	}

	const auto zoomFrom = _zoom.value;
	const auto targetBefore = _zoom.target;
	// A change of the limits moves the target, and the zoom follows it with the smoothing of the zoom.
	const auto zoomTo = StepZoom(RigZoomLimits(*limits), true);
	const auto zoomLimited = _zoom.target != targetBefore;
	// The ground range is below stage 0. A zoom out of it puts the camera at the top of the range first. Then the hand
	// rig continues from that camera, with a new pivot and a new spring arm.
	const auto endsInGround = zoomTo < 0.0f;
	const auto handZoomFrom = std::max(zoomFrom, 0.0f);
	const auto handZoomTo = std::max(zoomTo, 0.0f);
	// The view tilt at stage 0 for a raise. At stage 0 the hand rig and the ground range have the same view.
	const auto detailViewTilt = [this](float raise) {
		const auto tilt = StageTiltOf(0.0f);
		const auto view = SplitPitch(raise, tilt, PitchRangeFor(0.0f, tilt, PitchLimitsFromSettings()));
		return view.tilt - view.lookUp;
	};
	if (_ground && !endsInGround)
	{
		if (!StepGround(game, {.start = groundStart,
		                       .from = GroundPart(zoomFrom),
		                       .to = 0.0f,
		                       .yaw = rig.yaw,
		                       .yawStep = 0.0f,
		                       .userPitch = 0.0f,
		                       .pivot = {}}))
		{
			return false;
		}
		// In the ground range the zoom turns the view in place. The hand rig turns the rest of it about the pivot.
		rig.pitch = detailViewTilt(_pitchValue);
		_ground.reset();
		ResetArm();
		_pivot.reset();
		_pivotLimited = false;
		game.Debug("camera mod: the camera leaves the ground range");
		_groundExit = true;
	}
	_trace.zoomLimited = zoomLimited;
	_trace.zoomHeight = StageHeightOf(zoomTo);

	// The glides after a turn and a pitch stop at a new press, a land drag, a move of the game (the key pan) and the
	// start of a zoom.
	if (pressed || rightHeld || Length(shift) > k_GameShiftTolerance || _wheelNotch || keyZoom || input.reset)
	{
		_turnGlide.Stop();
		_pitchGlide.Stop();
	}

	// The game turns by the horizontal move of a middle button drag. The lock cancels the turn of a frame of a drag
	// that is mostly vertical.
	const auto turnLocked = _settings.pitchTurnLock && input.vertical != 0.0f &&
	                        std::abs(input.horizontal) < k_TurnLockRatio * std::abs(input.vertical);
	const auto userTurnedTo = turnLocked ? _startDirection : direction;
	_trace.turnLocked = turnLocked && std::abs(WrapDegrees(direction - _startDirection)) > k_TurnTolerance;
	// The glide measures the turn of the user. A short smoothing delays the turn of the user. When the glide starts, it
	// continues the turn at the measured rate in place of the turn that the smoothing did not give yet. So the speed of
	// the turn does not change at the release.
	const auto userTurn = WrapDegrees(userTurnedTo - _startDirection);
	const auto glideTurn = StepTurnGlide(userTurn, middleHeld);
	if (_turnGlide.active)
	{
		_turnInput.Reset();
	}
	const auto smoothTurn = _turnInput.Step(userTurn, _deltaTime, static_cast<float>(_settings.mouseSmoothing));
	const auto turnedTo = WrapDegrees(_startDirection + smoothTurn + glideTurn);
	auto yaw = turnedTo;
	if (_settings.rotateSmoothing > 0.0)
	{
		yaw = _heading.Step(_startDirection, turnedTo, _deltaTime, static_cast<float>(_settings.rotateSmoothing));
	}
	else
	{
		_heading.Reset(turnedTo);
	}
	const auto yawStep = WrapDegrees(yaw - rig.yaw);

	// The pitch: one raise value gives the tilt and, near the ground, the view up. The tilt of the stage row is the
	// tilt of a raise of zero, so a zoom changes the pitch as the game changes it.
	const auto groundPart = GroundPart(zoomTo);
	const auto detailTilt = StageTiltOf(0.0f);
	const auto stageTilt = endsInGround ? GroundTilt(detailTilt, groundPart) : StageTiltOf(zoomTo);
	_range = endsInGround ? GroundPitchRange(PitchRangeFor(0.0f, detailTilt, PitchLimitsFromSettings()), detailTilt, groundPart,
	                                         {.down = static_cast<float>(_settings.groundPitchDownDegrees),
	                                          .up = static_cast<float>(_settings.groundPitchUpDegrees)})
	                      : PitchRangeFor(zoomTo, stageTilt, PitchLimitsFromSettings());
	const auto lastPitchValue = _pitchValue;
	if (input.reset || (_settings.pitchResetOnZoom && std::abs(zoomTo - _lastStage) > k_StageTolerance))
	{
		_pitchTarget = 0.0f;
		_pitchInput.Reset();
	}
	_lastStage = zoomTo;
	const auto pitchSmoothing = static_cast<float>(_settings.pitchSmoothing);
	// The pitch glide takes the place of the smoothing at the release, as the turn glide does.
	const auto glideRaise = StepPitchGlide(input.raise, middleHeld);
	if (_pitchGlide.active)
	{
		_pitchInput.Reset();
	}
	const auto userRaise = _pitchInput.Step(input.raise, _deltaTime, static_cast<float>(_settings.mouseSmoothing));
	_pitchTarget = StepPitchTarget(_pitchTarget, userRaise + glideRaise, _range, _deltaTime, k_LookUpEaseSeconds);
	if ((glideRaise > 0.0f && _pitchTarget >= _range.highest) || (glideRaise < 0.0f && _pitchTarget <= _range.steepest))
	{
		// The glide of the pitch stops at the limit of the pitch.
		_pitchGlide.Stop();
	}
	_pitchValue = FollowValue(_pitchValue, _pitchTarget, _deltaTime, pitchSmoothing);
	_view = SplitPitch(_pitchValue, stageTilt, _range);
	const auto viewTilt = _view.tilt - _view.lookUp;
	const auto pitchStep = viewTilt - rig.pitch;
	// The part of the pitch that the user made: the pitch less the change that the zoom made to the tilt.
	const auto zoomView = SplitPitch(lastPitchValue, stageTilt, _range);
	const auto userPitch = viewTilt - (zoomView.tilt - zoomView.lookUp);
	_trace.raise = _pitchValue;
	_trace.raiseTarget = _pitchTarget;
	_trace.viewTilt = viewTilt;

	// The gesture and its pivot. A gesture starts with a press of the middle button, a wheel notch, or a turn, a
	// zoom or a pitch that starts with no press (the keys, the smoothing). The pivot is the land under the hand at
	// the start. With no land under the hand, the gesture keeps its pivot, or else uses the view centre. While the
	// user holds the middle button, the gesture keeps one pivot: a wheel notch, the keys and the smoothing do not
	// start a new gesture.
	const auto turning = std::abs(yawStep) > k_TurnTolerance;
	const auto zooming = std::abs(zoomTo - zoomFrom) > k_ZoomGestureStages;
	const auto pitching = std::abs(pitchStep) > k_PitchGestureDegrees;
	const auto gesture = middleHeld || turning || zooming || pitching;
	_frameInput.turning = turning;
	_frameInput.pitching = pitching;
	_frameInput.zooming = zooming;
	if (_grip && (turning || zooming || pitching))
	{
		// The camera of the grip is old now. The land drag takes the land under the cursor again in the next frame.
		_gripRetake = true;
	}
	if (pressed || (_wheelNotch && !middleHeld) || (gesture && !_pivot))
	{
		// Find the land under the cursor in the camera that the user saw, with the move of the game of this frame.
		if (!game.SetCamera({.position = ShownCamera(), .rotation = HandRigRotation(rig.yaw, rig.pitch)}))
		{
			return false;
		}
		// At a press, the hand on the screen is the pivot. The hand follows the cursor with a delay, so the rig takes
		// the place where the user sees the hand.
		_pressPoint = pressed ? PressPoint(game) : std::nullopt;
		auto land = _settings.pivotOnHand ? LandUnderCursor(game, _pressPoint) : std::nullopt;
		const auto lastPivot = _pivot;
		auto reason = PivotReason::Press;
		if (!pressed)
		{
			if (_wheelNotch && !middleHeld)
			{
				reason = PivotReason::Wheel;
			}
			else if (_groundExit)
			{
				reason = PivotReason::GroundExit;
			}
			else if (turning)
			{
				reason = PivotReason::Turn;
			}
			else if (zooming)
			{
				reason = PivotReason::Zoom;
			}
			else if (pitching)
			{
				reason = PivotReason::Pitch;
			}
		}
		if (land)
		{
			// A wheel notch zooms to the land under the hand at each distance. The other gestures turn about a point
			// that is not farther than the limit, on the line to the hand.
			_pivotLimited = false;
			_pivot = (_wheelNotch && !middleHeld) ? *land : LimitPivot(rig.position, *land);
			_pivotOnCursor = true;
			_level = land->y;
		}
		else if (!_pivot)
		{
			// In the ground range the focus of the game can be far away. The pivot stays near the camera.
			_pivot = endsInGround ? LimitPivot(rig.position, focus) : focus;
			_pivotOnCursor = false;
			_pivotLimited = false;
			_level = focus.y;
		}
		if (_pivot && (!lastPivot || !SamePoint(*lastPivot, *_pivot)))
		{
			_trace.pivotReason = reason;
			const auto retaken = lastPivot && middleHeld && !pressed;
			// A wheel notch takes a pivot for each notch. The column tells it, the log does not.
			if (reason != PivotReason::Wheel || retaken)
			{
				Tell(CameraEventKind::Pivot,
				     std::format("pivot {} ({}) at {} {}", retaken ? "re-taken" : "taken", Name(reason), FormatPoint(*_pivot),
				                 land ? (_pressPoint ? "under the hand" : "under the cursor") : "at the focus of the game"));
			}
		}
	}
	const auto pivot = _pivot.value_or(focus);
	const auto armClamped = std::exchange(_armClamped, false);
	if (arm && gesture && !endsInGround && (!_armPivot || !SamePoint(*_armPivot, pivot)))
	{
		// The spring arm turns about the pivot of the gesture. After the clamp into the map moved the camera, the arm
		// goes through the pivot again; the distance to the pivot changed with the move of the camera.
		MoveArmPivot(pivot, armClamped && !_trace.pivotReason);
	}
	_trace.turned = turning;
	_trace.pivot = _pivot;
	_trace.pivotLimited = _pivot && _pivotLimited;

	// The moves about the pivot. Each one is a rigid move of the camera that keeps the pivot at the same place on the
	// screen: the turn about the vertical axis, the move on the line to the pivot, and the turn about the horizontal
	// axis.
	if (endsInGround)
	{
		if (handZoomFrom != handZoomTo)
		{
			// The part of the zoom above stage 0 moves about the pivot, as in the hand rig.
			const auto factor = ZoomFactor(rig.position, pivot, handZoomFrom, handZoomTo);
			rig.position = ZoomAboutPivot(rig.position, pivot, factor);
			_armLength *= factor;
			_trace.zoomFactor = factor;
			// The tilt of the stage changes down to stage 0 about the pivot too. Below stage 0 the zoom turns the view in
			// place.
			const auto tiltStep = detailViewTilt(lastPitchValue) - rig.pitch;
			rig.position = PitchAboutPivot(rig.position, pivot, rig.yaw, tiltStep);
			rig.pitch += tiltStep;
		}
		const auto entered = !_ground;
		if (entered && !EnterGround(game))
		{
			return false;
		}
		if (!StepGround(game, {.start = entered ? rig.position : groundStart,
		                       .from = GroundPart(zoomFrom),
		                       .to = groundPart,
		                       .yaw = yaw,
		                       .yawStep = turning ? yawStep : 0.0f,
		                       .userPitch = userPitch,
		                       .pivot = _pivot}))
		{
			return false;
		}
		rig.yaw = yaw;
		rig.pitch = viewTilt;
		if (!gesture)
		{
			_pivot.reset();
			_pivotLimited = false;
		}
		// In the ground range the camera is the rig, with no chase.
		StepChase(game, {});
		if (!WriteHandState(game, shake, shakeTurn))
		{
			return false;
		}
		LockCursor(game, middleHeld, pressed);
		TrackPosition(game, TargetCamera());
		if (!_cursorLock)
		{
			EaseHand(game);
		}
		_trace.applied = true;
		return true;
	}
	if (turning)
	{
		rig.position = YawAboutPivot(rig.position, pivot, yawStep);
	}
	rig.yaw = yaw;
	if (handZoomTo != handZoomFrom)
	{
		// A zoom changes the desired distance of the rig and the length of the spring arm by the same factor. It never
		// starts from the camera that an obstacle pulled in, because the ease of the zoom continues for many frames,
		// and each of them would make the desired distance shorter.
		const auto factor = ZoomFactor(rig.position, pivot, handZoomFrom, handZoomTo);
		rig.position = ZoomAboutPivot(rig.position, pivot, factor);
		_armLength *= factor;
		if (_desired)
		{
			*_desired *= factor;
			_trace.desiredChange = 1;
		}
		_trace.zoomFactor = factor;
	}
	const auto clearance = static_cast<float>(_settings.groundClearance);
	const auto keepAboveLand = !game.IgnoresCollisions();
	if (pitchStep != 0.0f)
	{
		// A pitch that would put the camera into the land turns about the pivot only as far as the land permits. The
		// remaining part turns the camera about its own position, so the view continues to turn (for example up to
		// the horizon near the ground).
		// The land test uses the camera on the spring arm, which the user sees. With the spring arm, the floor is the
		// exact land plus the clearance, as in the spring arm.
		const auto start = ArmCamera();
		auto permitted = [&](float part) -> std::optional<bool> {
			const auto position = PitchAboutPivot(start, pivot, rig.yaw, pitchStep * part);
			auto ground = arm ? game.LandHeight(position) : game.HighestGround(position);
			if (!ground)
			{
				return std::nullopt;
			}
			return position.y >= *ground + clearance || position.y >= start.y;
		};
		float part = 1.0f;
		if (keepAboveLand)
		{
			auto full = permitted(1.0f);
			if (!full)
			{
				return false;
			}
			if (!*full)
			{
				float low = 0.0f;
				float high = 1.0f;
				for (int step = 0; step < k_PitchLimitSteps; ++step)
				{
					const auto middlePart = (low + high) * 0.5f;
					auto valid = permitted(middlePart);
					if (!valid)
					{
						return false;
					}
					(*valid ? low : high) = middlePart;
				}
				part = low;
			}
		}
		rig.position = PitchAboutPivot(rig.position, pivot, rig.yaw, pitchStep * part);
		_trace.inPlaceTilt = pitchStep * (1.0f - part);
		_trace.pivotKept = part >= 1.0f;
	}
	rig.pitch = viewTilt;

	if (arm)
	{
		// The spring arm keeps the camera in front of the land and above it (and in front of the objects, if the
		// object collision is on).
		// The land on the line from the pivot to the camera stops the arm only in a gesture of the user about the pivot.
		// In a pan, a glide after a turn or a pitch, or with no input, a ridge between the view centre and the camera
		// does not pull the camera in.
		const auto userGesture = middleHeld || zooming || ((turning || pitching) && glideTurn == 0.0f && glideRaise == 0.0f);
		if (!StepArm(game, keepAboveLand, userGesture))
		{
			return false;
		}
	}
	else if (keepAboveLand)
	{
		// Keep the camera above the land with the rule of the game, so that the push of the game in the next frame has
		// nothing to do.
		auto ground = game.HighestGround(rig.position);
		if (!ground)
		{
			return false;
		}
		if (rig.position.y < *ground + clearance)
		{
			rig.position.y = *ground + clearance;
			_trace.pivotKept = false;
		}
	}
	if (!gesture)
	{
		_pivot.reset();
		_pivotLimited = false;
	}

	// The camera that the user sees follows the moves of the land drag, the key pan, the edge scroll and the glide.
	StepChase(game, shift + gripMove);
	if (!WriteHandState(game, shake, shakeTurn))
	{
		return false;
	}
	LockCursor(game, middleHeld, pressed);
	TrackPosition(game, TargetCamera());
	if (!_cursorLock)
	{
		EaseHand(game);
	}
	_trace.applied = true;
	return true;
}

bool CameraLoop::WriteHandState(CameraGame& game, WorldPoint shake, Quaternion shakeTurn)
{
	auto& rig = *_rig;
	// The game gets the state of the target camera, so that its pan and its clamp act on the target. The transform
	// gets the camera that the user sees.
	const auto camera = TargetCamera();
	CameraPlacement shown {.position = camera + _chase.lag + shake,
	                       .rotation = Multiply(shakeTurn, HandRigRotation(rig.yaw, rig.pitch))};
	if (!game.SetCamera(shown))
	{
		game.ReportError("the mod could not move the camera transform");
		return false;
	}
	auto state = GameStateFor(camera);
	const auto writeState = [&] {
		game.SetFocus(state.focus);
		game.SetDirection(rig.yaw);
		// In the ground range the game stays at its closest zoom stage.
		const auto stage = std::max(_zoom.value, 0.0f);
		game.SetStage(stage);
		game.SetStageView({.height = state.height, .tilt = state.tilt});
		_focusWritten = state.focus;
		_stageWritten = stage;
	};
	writeState();

	const auto fields = game.Drag();
	if (!fields.inSubScene && fields.ignoreHand == 0)
	{
		// The game keeps its focus in the map. It moves the focus and the transform back into the map.
		for (int attempt = 0; attempt < k_ClampAttempts; ++attempt)
		{
			game.ClampFocus();
			const auto clamped = game.Focus();
			const auto clampShift = clamped - state.focus;
			if (Length(clampShift) <= 0.0f)
			{
				break;
			}
			// A flat view, or a view up, has its focus far away on the line of the heading, and that point can be out of
			// the map while the camera is in it. A nearer focus on the same line keeps the camera where it is: the game
			// then looks down more steeply, but its camera stays the camera of the user.
			const WorldPoint heading {std::sin(rig.yaw * k_DegreesToRadians), 0.0f, std::cos(rig.yaw * k_DegreesToRadians)};
			// The clamp put each axis that was out of the map on the edge of the map. The farthest focus on the line
			// of the heading that is in the map meets the nearest of these edges.
			auto along = std::numeric_limits<float>::max();
			if (clampShift.x != 0.0f && std::abs(heading.x) > k_PlacementTolerance)
			{
				along = std::min(along, (clamped.x - camera.x) / heading.x);
			}
			if (clampShift.z != 0.0f && std::abs(heading.z) > k_PlacementTolerance)
			{
				along = std::min(along, (clamped.z - camera.z) / heading.z);
			}
			along -= k_PlacementTolerance;
			const auto current = Dot(state.focus - camera, heading);
			if (attempt + 1 < k_ClampAttempts && along > k_SmallestClampReach && along < current - k_PlacementTolerance)
			{
				if (!game.SetCamera(shown))
				{
					game.ReportError("the mod could not move the camera transform");
					return false;
				}
				state = GameStateFor(camera, along);
				writeState();
				continue;
			}
			// The camera itself is at the edge of the map. The rig takes the move of the game.
			rig.position = rig.position + clampShift;
			if (_armPivot)
			{
				_armPivot = *_armPivot + clampShift;
				_armClamped = true;
			}
			if (ChaseOn() && !_resyncing && Length(clampShift) > k_ClampChaseMetres)
			{
				// The camera that the user sees follows a large clamp with the chase, two times slower, and does not jump.
				// A small clamp (a camera that stays at the edge) and a start from the camera of the game move the camera at
				// once, so that the chase and the clamp do not shake the camera.
				_chase.lag = _chase.lag - clampShift;
				_chaseSlowLeft = static_cast<float>(_settings.chaseSeconds);
				if (!game.SetCamera(shown))
				{
					game.ReportError("the mod could not move the camera transform");
					return false;
				}
			}
			else
			{
				shown.position = shown.position + clampShift;
			}
			_focusWritten = clamped;
			_trace.pivotKept = false;
			break;
		}
	}
	if (!_ground)
	{
		// The camera that the user sees is never under the land, also in the frame where the clamp moved the rig. The
		// camera ball can keep the target nearer to the land than the clearance, so the limit is the land itself; the
		// chase keeps the clearance (`StepChase`).
		auto land = game.LandHeight(shown.position - shake);
		if (!land)
		{
			return false;
		}
		if (const auto rise = *land - (shown.position.y - shake.y); rise > 0.0f)
		{
			shown.position.y += rise;
			if (ChaseOn())
			{
				_chase.lag.y += rise;
				_chase.velocity.y = std::max(_chase.velocity.y, 0.0f);
			}
			if (!game.SetCamera(shown))
			{
				game.ReportError("the mod could not move the camera transform");
				return false;
			}
		}
	}
	_written = shown;
	_shown = shown.position;
	_trace.writtenFocus = _focusWritten;
	_trace.writtenCamera = shown;
	return true;
}

GameCameraState CameraLoop::GameStateFor(WorldPoint camera, std::optional<float> reachLimit) const
{
	// The state of the game places the camera of the game at the camera: the focus is on the view centre at the
	// height of the pivot, and the height and the tilt are the values that the game uses to place the camera.
	// The focus is at most the reach away. The reach is sufficient for each view that looks down as steeply as the
	// flattest row of the stage table. Then the camera of the game has the rotation of the rig, and the land drag of
	// the game uses the camera that the user sees.
	const auto& rig = *_rig;
	auto reach = InPlaceFocusReach(StageHeightOf(_zoom.value), StageTiltOf(_zoom.value));
	if (_flattestStageTilt >= k_FlattestZoomTilt)
	{
		reach = std::max(reach, (camera.y - _level) / std::tan(_flattestStageTilt * k_DegreesToRadians));
	}
	if (reachLimit)
	{
		reach = std::min(reach, *reachLimit);
	}
	return GameStateForCamera(camera, rig.yaw, rig.pitch, _level, reach);
}

WorldPoint CameraLoop::ArmCamera() const
{
	const auto& rig = _rig->position;
	if (!_armPivot)
	{
		return rig;
	}
	const auto toRig = rig - *_armPivot;
	const auto length = Length(toRig);
	if (length <= k_PlacementTolerance || _armLength >= length - k_ArmSnap)
	{
		// The arm is not shorter than the rig. Use the rig position exactly, with no rounding error.
		return rig;
	}
	return *_armPivot + toRig * (_armLength / length);
}

WorldPoint CameraLoop::TargetCamera() const
{
	return ArmCamera() + _armPush;
}

WorldPoint CameraLoop::ShownCamera() const
{
	return TargetCamera() + _chase.lag;
}

void CameraLoop::ResetArm()
{
	_armTouch.reset();
	_armTouchName.clear();
	_armPivot.reset();
	_armClamped = false;
	_armLength = 0.0f;
	_armPush = {};
	_desired.reset();
}

void CameraLoop::MoveArmPivot(WorldPoint pivot, bool clamp)
{
	// The shown camera does not move. The new arm goes from the new pivot through the shown camera, and the rig is on
	// that line. The rig keeps the part of the distance that the obstacles took from the old arm. Thus the camera moves
	// back out by the same distance when the line is clear.
	auto& rig = *_rig;
	const auto shown = TargetCamera();
	const auto excess = _armPivot ? std::max(Length(rig.position - *_armPivot) - _armLength, 0.0f) : 0.0f;
	const auto toShown = shown - pivot;
	const auto length = Length(toShown);
	const auto before = _desired;
	_trace.desiredBefore = before;
	auto tell = [&] {
		Tell(CameraEventKind::Desired,
		     std::format("desired {} -> {:.2f} m ({})", before ? std::format("{:.2f}", *before) : std::string("none"),
		                 _desired.value_or(0.0f), clamp ? "the clamp into the map moved the camera" : "new pivot"));
	};
	_armPivot = pivot;
	// The new arm goes through the camera that the land pushed. The push is now part of the arm, so the camera does not
	// move back.
	_armPush = {};
	if (length <= k_PlacementTolerance)
	{
		rig.position = shown;
		_armLength = 0.0f;
		_desired = 0.0f;
		_trace.desiredChange = clamp ? 6 : 2;
		tell();
		return;
	}
	_armLength = length;
	rig.position = excess > 0.0f ? pivot + toShown * ((length + excess) / length) : shown;
	_desired = Length(rig.position - pivot);
	_trace.desiredChange = clamp ? 6 : 2;
	tell();
}

std::optional<CameraLoop::ArmLimit> CameraLoop::FindArmLimit(CameraGame& game, WorldPoint pivot, WorldPoint direction,
                                                             float length, bool keepAboveLand)
{
	ArmLimit limit {.target = length, .hard = length, .hit = std::nullopt};
	if (keepAboveLand)
	{
		// The land. Go out from the pivot, and find the farthest point that is at least the clearance above the exact
		// land. The arm stops where the line goes into the land after it was clear of the land. The pivot is on the
		// land, so the line can touch the land near the pivot. Before the line is clear, only a deep point stops the
		// arm. The samples are near each other at the pivot and farther apart near the camera.
		const auto clearance = static_cast<float>(_settings.groundClearance);
		const auto distances = ArmSampleDistances(length, {.firstStep = k_ArmFirstLandStep,
		                                                   .growth = k_ArmLandStepGrowth,
		                                                   .maximumPoints = static_cast<int>(_settings.armLandSamples)});
		int samples = 0;
		int ridgeSamples = 0;
		bool failed = false;
		// The height of the line above the exact land at a distance from the pivot.
		auto gapAt = [&](float distance) -> std::optional<float> {
			const auto point = pivot + direction * distance;
			++samples;
			auto land = game.LandHeight(point);
			if (!land)
			{
				failed = true;
				return std::nullopt;
			}
			return point.y - *land;
		};
		// The land between two samples that are both above the land. If the line is low above the land at the two
		// samples, a ridge between them can go over the line, so the arm examines the middle, and then each half.
		// Return the distance of a point under the land, or nothing.
		auto ridge = [&](auto& self, float from, float fromGap, float to, float toGap) -> std::optional<float> {
			if (failed || to - from < k_ArmRidgeStep || ridgeSamples >= k_ArmRidgeSamples ||
			    fromGap + toGap >= k_ArmRidgeSlope * (to - from))
			{
				return std::nullopt;
			}
			const auto middle = (from + to) * 0.5f;
			++ridgeSamples;
			auto gap = gapAt(middle);
			if (!gap)
			{
				return std::nullopt;
			}
			if (*gap < 0.0f)
			{
				return middle;
			}
			if (auto under = self(self, from, fromGap, middle, *gap))
			{
				return under;
			}
			return self(self, middle, *gap, to, toGap);
		};
		std::optional<float> lastClear;
		std::optional<float> lastAbove;
		std::optional<float> previousGap;
		float previous = 0.0f;
		bool blocked = false;
		bool endClear = false;
		for (const auto distance : distances)
		{
			auto gap = gapAt(distance);
			if (!gap)
			{
				return std::nullopt;
			}
			endClear = false;
			if (*gap < 0.0f)
			{
				if (lastAbove && (lastClear || -*gap > k_ArmLandContactDepth))
				{
					blocked = true;
					break;
				}
				previous = distance;
				previousGap.reset();
				continue;
			}
			if (lastClear && previousGap && ridge(ridge, previous, *previousGap, distance, *gap))
			{
				blocked = true;
				break;
			}
			if (failed)
			{
				return std::nullopt;
			}
			lastAbove = distance;
			if (*gap >= clearance)
			{
				lastClear = distance;
				endClear = true;
			}
			previous = distance;
			previousGap = gap;
		}
		if (failed)
		{
			return std::nullopt;
		}
		_trace.armLandSamples = samples;
		if (blocked || !endClear)
		{
			// With no point above the clearance, the arm stops before the land, and the camera ball keeps the camera
			// out of the land.
			const auto land = lastClear ? *lastClear : (blocked ? lastAbove.value_or(0.0f) : length);
			limit.target = land;
			limit.hard = land;
		}
	}

	// The objects. The game finds them with the collision test of its camera. Without that test, or with the object
	// collision off, the arm uses only the land and does no sphere test.
	const auto start = std::min(k_ArmCastStart, length);
	if (_settings.objectCollision && length - start > 0.0f)
	{
		// The land is not in the layers: the land test above keeps the arm out of the land.
		auto cast = game.CastSphere(pivot + direction * start, direction, k_ArmRadius, length - start, k_CameraObstacleLayers);
		auto from = start;
		if (cast && cast->hit && start + cast->distance < k_ArmPivotContact)
		{
			// The sphere started in an object, or the pivot is on an object. Start again after the hit.
			from = std::min(start + cast->distance + k_ArmCastRetry, length);
			cast = length - from > 0.0f ? game.CastSphere(pivot + direction * from, direction, k_ArmRadius, length - from,
			                                              k_CameraObstacleLayers)
			                            : std::optional<SphereHit> {};
			if (cast && cast->hit && cast->distance <= k_ArmOverlap)
			{
				cast.reset();
			}
		}
		if (cast && cast->hit)
		{
			const auto hit = from + cast->distance;
			const auto margin = static_cast<float>(_settings.collisionMargin);
			if (hit < limit.hard)
			{
				limit.collider = cast->collider != 0 ? cast->collider : k_UnknownObject;
			}
			limit.hard = std::min(limit.hard, hit);
			limit.target = std::min(limit.target, hit - margin);
		}
	}
	limit.hard = std::clamp(limit.hard, 0.0f, length);
	limit.target = std::clamp(limit.target, 0.0f, limit.hard);
	if (limit.hard < length)
	{
		limit.hit = limit.hard;
	}
	return limit;
}

void CameraLoop::TellArmHit(CameraGame& game, const ArmLimit& limit)
{
	const auto touch = limit.hit ? std::optional<uint64_t>(limit.collider) : std::nullopt;
	if (touch != _armTouch)
	{
		if (touch)
		{
			// The name comes from the game only when the arm touches another object, not in each frame.
			_armTouchName = *touch == 0 ? std::string("the land")
			                            : (*touch == k_UnknownObject ? std::string("an object")
			                                                         : game.ColliderName(*touch).value_or("an object"));
			Tell(CameraEventKind::Arm, std::format("arm hit '{}' at {:.1f} m", _armTouchName, *limit.hit));
		}
		else
		{
			Tell(CameraEventKind::Arm, std::format("arm clear of '{}'", _armTouchName));
			_armTouchName.clear();
		}
		_armTouch = touch;
	}
	_trace.armHitName.Assign(_armTouchName);
}

bool CameraLoop::StepArm(CameraGame& game, bool keepAboveLand, bool lineTest)
{
	auto& rig = *_rig;
	if (!_armPivot)
	{
		// With no pivot of a gesture, the arm keeps the camera on the line to the focus of the game.
		_armPivot = GameStateFor(rig.position).focus;
		_armLength = Length(rig.position - *_armPivot);
		_desired.reset();
	}
	const auto pivot = *_armPivot;
	if (!_desired)
	{
		_desired = Length(rig.position - pivot);
		_trace.desiredChange = 3;
		Tell(CameraEventKind::Desired, std::format("desired none -> {:.2f} m (new arm)", *_desired));
	}
	else if (const auto now = Length(rig.position - pivot);
	         std::abs(now - *_desired) > k_DesiredTolerance * std::max(*_desired, 1.0f) && now > k_PlacementTolerance)
	{
		// Only a zoom and a new pivot may change the desired distance. A turn and a pitch are rigid moves about the
		// pivot, and an obstacle only makes the arm shorter. Put the rig back at the desired distance on its line.
		game.Debug(std::format("camera mod: the desired distance of the spring arm changed from {:.3f} to {:.3f} m with no "
		                       "zoom; the rig goes back",
		                       *_desired, now));
		rig.position = pivot + (rig.position - pivot) * (*_desired / now);
		_trace.desiredChange = 4;
		_trace.desiredBefore = now;
		Tell(CameraEventKind::Desired,
		     std::format("desired {:.2f} -> {:.2f} m (correction: a change with no zoom)", now, *_desired));
	}
	// The distance from the pivot to the camera of the game at the zoom stage, and the limits that it gives.
	const auto stageTilt = std::max(StageTiltOf(_zoom.value), k_FlattestZoomTilt);
	const auto stageDistance = StageHeightOf(_zoom.value) / std::sin(stageTilt * k_DegreesToRadians);
	const auto smallestDesired = std::max(k_SmallestDesiredMetres, k_SmallestDesiredPart * stageDistance);
	if (Length(rig.position - pivot) < smallestDesired)
	{
		// The rig has almost no distance to the pivot: it cannot turn, pitch or zoom about it. Put the rig back at
		// the distance of the camera of the game at the zoom stage, on the line from the pivot, or else against the
		// view direction.
		const auto toRig = rig.position - pivot;
		const auto line = Length(toRig) > k_PlacementTolerance
		                      ? Normalize(toRig)
		                      : Rotate(HandRigRotation(rig.yaw, rig.pitch), WorldPoint {0.0f, 0.0f, -1.0f});
		rig.position = pivot + line * stageDistance;
		// The camera starts at its current distance and goes out with the ease of the arm, so it does not jump.
		_armLength = std::min(Length(toRig), stageDistance);
		_trace.desiredBefore = _desired;
		Tell(CameraEventKind::Desired,
		     std::format("desired {:.2f} -> {:.2f} m (the arm had no length)", _desired.value_or(0.0f), stageDistance));
		_desired = stageDistance;
		_trace.desiredChange = 5;
		_trace.pivotKept = false;
		game.Debug("camera mod: the spring arm had no length; the rig goes back to the distance of the zoom stage");
	}
	const auto toRig = rig.position - pivot;
	const auto desired = Length(toRig);
	const auto lastPush = std::exchange(_armPush, WorldPoint {});
	_trace.desiredDistance = desired;
	auto limit = FindArmLimit(game, pivot, toRig * (1.0f / desired), desired, keepAboveLand && lineTest);
	if (!limit)
	{
		return false;
	}
	// The arm never becomes shorter than a part of the distance of the zoom stage. Where an obstacle is nearer than
	// that, the camera rises above the land instead (below).
	const auto shortest = std::min(desired, std::max(k_ShortestArmMetres, k_ShortestArmPart * stageDistance));
	const auto nearPivot = limit->hard < shortest;
	limit->target = std::max(limit->target, shortest);
	limit->hard = std::max(limit->hard, shortest);
	_trace.armHit = limit->hit;
	TellArmHit(game, *limit);
	// The arm moves in fast and out slowly. It never goes past an obstacle on the arm. An obstacle that is nearer to the
	// pivot than the shortest arm cannot stop the camera. It only pulls the arm in with the slow ease, so the camera
	// does not jump far in one frame.
	const auto lastLength = std::min(_armLength, desired);
	auto length = lastLength;
	const auto seconds =
	    limit->target < length && !nearPivot ? _settings.collisionInSmoothing : _settings.collisionOutSmoothing;
	length = FollowValue(length, limit->target, _deltaTime, static_cast<float>(seconds));
	if (std::abs(length - limit->target) < k_ArmSnap)
	{
		length = limit->target;
	}
	_armLength = nearPivot ? std::max(length, 0.0f) : std::clamp(length, 0.0f, limit->hard);
	// The land never pulls the camera in faster than half the desired distance for each second (at least the smallest
	// speed of the chase), so the land never throws the camera. The camera can then be behind the land for a short time;
	// the camera ball and the floor keep it above the land. An object (with the object collision) stops the camera at
	// once, as before. The return out follows the ease of the arm, with only a high limit of the speed.
	const auto minimumSpeed = static_cast<float>(_settings.chaseMinSpeed);
	if (limit->collider == 0)
	{
		_armLength = std::max(_armLength, lastLength - std::max(minimumSpeed, k_ArmPullDistances * desired) * _deltaTime);
	}
	_armLength = std::min(_armLength, lastLength + std::max(minimumSpeed, k_ArmReturnDistances * desired) * _deltaTime);
	_trace.armDistance = _armLength;

	if (keepAboveLand)
	{
		// The camera ball and the floor push the camera on the arm out of the land. A pushed camera must not be farther
		// from the pivot than the desired distance, so the arm becomes shorter until it is not.
		auto pushAt = [&](float armLength, int passes) {
			return LandPushAt(game, pivot + toRig * (armLength / desired), passes);
		};
		auto tooFar = [&](float armLength, WorldPoint push) {
			const auto camera = pivot + toRig * (armLength / desired) + push;
			return Length(camera - pivot) > desired + k_ArmSnap;
		};
		auto push = pushAt(_armLength, k_LandBallPasses);
		if (!push)
		{
			return false;
		}
		// One owner of the length while the camera ball pushes: an arm that grows into a push that is too far stays at the
		// length of the last frame, if that length is correct within a dead band. It becomes shorter only when the push
		// of the last length is too far by more than the band. Else the ease out of the arm and the eased push of the
		// ball made a cycle of moves in the game.
		bool kept = false;
		if (Length(*push) > 0.0f && _armLength > lastLength && tooFar(_armLength, *push))
		{
			auto pushLast = pushAt(lastLength, k_LandBallPasses);
			if (!pushLast)
			{
				return false;
			}
			const auto reach = Length(pivot + toRig * (lastLength / desired) + *pushLast - pivot);
			if (reach <= desired + k_ArmPushBand)
			{
				_armLength = lastLength;
				_trace.armDistance = _armLength;
				push = pushLast;
				kept = true;
			}
		}
		if (!kept && Length(*push) > 0.0f && tooFar(_armLength, *push) && _armLength > shortest)
		{
			float low = shortest;
			float high = _armLength;
			for (int step = 0; step < k_ArmLiftSteps && high - low > k_ArmLiftPrecision; ++step)
			{
				const auto middle = (low + high) * 0.5f;
				auto pushThere = pushAt(middle, 1);
				if (!pushThere)
				{
					return false;
				}
				(tooFar(middle, *pushThere) ? high : low) = middle;
			}
			// Keep the length of the last frame when it is almost the same and still correct, so that the camera
			// stops.
			if (auto pushThere = pushAt(lastLength, 1);
			    pushThere && std::abs(low - lastLength) <= k_ArmSnap && !tooFar(lastLength, *pushThere))
			{
				low = lastLength;
			}
			_armLength = low;
			_trace.armDistance = _armLength;
			push = pushAt(_armLength, k_LandBallPasses);
			if (!push)
			{
				return false;
			}
		}
		// The push eases out of the land and back, from the push of the last frame, so that the camera does not jump.
		// Off, the camera moves at once.
		_armPush = _settings.panAvoidance
		               ? EasePush(lastPush, *push, _deltaTime, static_cast<float>(_settings.terrainPushSmoothing),
		                          static_cast<float>(_settings.terrainReturnSmoothing), k_LandPushSpeed)
		               : *push;
		if (Length(_armPush - *push) < k_ArmSnap)
		{
			// The ease never gets to its target. Stop it near the target, so that the camera stops.
			_armPush = *push;
		}
		const auto arm = ArmCamera();
		auto shown = arm + _armPush;
		if (const auto reach = Length(shown - pivot); reach > desired + k_ArmPushBand)
		{
			// An eased push that goes back slowly can be farther from the pivot than the desired distance. The limit has
			// the dead band of the length of the arm, so that the two rules do not move the camera in turn.
			shown = pivot + (shown - pivot) * ((desired + k_ArmPushBand) / reach);
		}
		// The floor: the camera that the user sees is never below the land plus the clearance. A camera under it rises at
		// once.
		auto floored = AboveFloor(game, shown);
		if (!floored)
		{
			return false;
		}
		_armPush = *floored - arm;
		_trace.armLift = Length(_armPush);
	}
	if (Length(_armPush) > 0.0f || Length(_armPush - lastPush) > k_ArmSnap)
	{
		// The camera is off the arm, or it was in the last frame, so the pivot moved on the screen.
		_trace.pivotKept = false;
	}
	return true;
}

std::optional<WorldPoint> CameraLoop::AboveFloor(CameraGame& game, WorldPoint camera)
{
	auto land = game.LandHeight(camera);
	if (!land)
	{
		return std::nullopt;
	}
	camera.y = std::max(camera.y, *land + static_cast<float>(_settings.groundClearance));
	return camera;
}

std::optional<WorldPoint> CameraLoop::LandPushAt(CameraGame& game, WorldPoint camera, int passes)
{
	// The ball of points around the camera, turned with the view, pushes the camera out of the land along the land
	// normal. Then the floor keeps it above the land.
	auto height = [&](float x, float z) { return game.LandHeight({x, camera.y, z}); };
	auto ball = PushOutOfLand(height, camera, HandRigRotation(_rig->yaw, _rig->pitch),
	                          static_cast<float>(_settings.cameraClearanceRadius), passes);
	if (!ball)
	{
		return std::nullopt;
	}
	_trace.landPushes = ball->pushes;
	auto floored = AboveFloor(game, ball->position);
	if (!floored)
	{
		return std::nullopt;
	}
	return *floored - camera;
}

WorldPoint CameraLoop::LimitPivot(WorldPoint camera, WorldPoint land)
{
	if (_settings.maxPivotDistance <= 0.0)
	{
		return land;
	}
	// The limit is a multiple of the distance from the camera to the focus of the game at the zoom stage.
	const auto stageTilt = std::max(StageTiltOf(_zoom.value), k_FlattestZoomTilt);
	const auto stageDistance = StageHeightOf(_zoom.value) / std::sin(stageTilt * k_DegreesToRadians);
	const auto limit = static_cast<float>(_settings.maxPivotDistance) * stageDistance;
	const auto toLand = land - camera;
	const auto distance = Length(toLand);
	if (distance <= limit)
	{
		return land;
	}
	// The pivot stays on the line from the camera to the hand, so it is at the same place on the screen as the hand.
	_pivotLimited = true;
	return camera + toLand * (limit / distance);
}

float CameraLoop::ZoomFactor(WorldPoint camera, WorldPoint pivot, float zoomFrom, float zoomTo) const
{
	const auto heightRatio = StageHeightOf(zoomTo) / StageHeightOf(zoomFrom);
	const auto toPivot = pivot - camera;
	const auto distance = Length(toPivot);
	if (distance <= k_PlacementTolerance)
	{
		return heightRatio;
	}
	// The pivot stays at its place on the screen, so its direction in the camera does not change. The game camera of
	// a stage, at the tilt of that stage, holds the pivot at that place at the distance `height / sin(angle)`, where
	// the angle is the angle of the line to the pivot below the horizon. A pitch about the pivot keeps the distance.
	const auto inCamera = Rotate(Inverse(HandRigRotation(_rig->yaw, _rig->pitch)), toPivot * (1.0f / distance));
	auto sineAt = [&](float tilt) { return std::max(-Rotate(PitchRotation(tilt), inCamera).y, k_SteepestPivotSine); };
	const auto from = StageHeightOf(zoomFrom) / sineAt(StageTiltOf(zoomFrom));
	const auto to = StageHeightOf(zoomTo) / sineAt(StageTiltOf(zoomTo));
	// The camera can be farther from the pivot than the game camera of the stage (a pitch about another pivot, the
	// land, the edge of the map). The zoom keeps less of that difference at each stage, so a zoom over many stages
	// does not multiply it.
	const auto excess = std::max(distance / from, k_MinimumZoomExcess);
	const auto keep = std::exp(-k_ZoomExcessDecay * std::abs(zoomTo - zoomFrom));
	return to / from * std::pow(excess, keep - 1.0f);
}

std::optional<WorldPoint> CameraLoop::PressPoint(CameraGame& game)
{
	auto cursor = game.InputPosition();
	if (!cursor || !_settings.lockCursor)
	{
		return cursor;
	}
	auto hand = game.Hand();
	if (!hand || !hand->visible || hand->overUi || (hand->point.x == 0.0f && hand->point.y == 0.0f && hand->point.z == 0.0f))
	{
		return cursor;
	}
	auto screen = game.ScreenPointOf(hand->point);
	if (!screen || screen->z <= 0.0f || std::hypot(screen->x - cursor->x, screen->y - cursor->y) > k_HandPressPixels)
	{
		// The hand is not near the cursor (for example, the game hid it or did not move it). Use the cursor.
		return cursor;
	}
	return WorldPoint {screen->x, screen->y, 0.0f};
}

void CameraLoop::LockCursor(CameraGame& game, bool middleHeld, bool pressed)
{
	const auto wasLocked = _cursorLock.has_value();
	KeepCursor(game, middleHeld, pressed);
	if (!middleHeld)
	{
		if (wasLocked)
		{
			Tell(CameraEventKind::CursorLock, "cursor lock off (release)");
		}
		_cursorLockTold = false;
		return;
	}
	if (_cursorLock && !wasLocked)
	{
		Tell(CameraEventKind::CursorLock, std::format("cursor lock on at ({:.0f}, {:.0f}) px", _cursorLock->x, _cursorLock->y));
	}
	if (!_settings.lockCursor || _cursorLock)
	{
		return;
	}
	// The user holds the middle button, so the rig must keep the cursor on the pivot. Tell once for each cause why it
	// does not.
	const auto cause = _cursorLockOff.value_or(CursorLockOff::Unknown);
	_trace.cursorLockOff = static_cast<int>(cause);
	if (!std::exchange(_cursorLockTold, true))
	{
		Tell(CameraEventKind::CursorLock, std::format("cursor lock off ({})", Describe(cause)));
	}
	const auto bit = 1U << static_cast<unsigned>(cause);
	if ((_cursorLockOffTold & bit) == 0U)
	{
		_cursorLockOffTold |= bit;
		game.Debug(std::format("camera mod: the middle button is held, but the hand rig does not keep the cursor on the "
		                       "pivot: {}",
		                       Describe(cause)));
	}
}

void CameraLoop::KeepCursor(CameraGame& game, bool middleHeld, bool pressed)
{
	if (!middleHeld || !_settings.lockCursor)
	{
		// After the release, the cursor and the hand continue from the place where the rig kept them.
		_cursorLock.reset();
		_cursorLockOff.reset();
		return;
	}
	if (pressed)
	{
		// A new press starts a new lock.
		_cursorLock.reset();
		_cursorLockOff.reset();
	}
	if (_cursorLockOff)
	{
		return;
	}
	auto input = game.InputPosition();
	auto mouse = game.MousePosition();
	if (!input || !mouse)
	{
		_cursorLock.reset();
		_cursorLockOff = CursorLockOff::NoInputPosition;
		return;
	}
	if (!_cursorLock)
	{
		auto hand = game.Hand();
		if (!pressed || (hand && hand->overUi))
		{
			// The press was on the UI, or the rig did not keep the cursor from the press. The cursor stays free until
			// the release.
			_cursorLockOff = pressed ? CursorLockOff::PressOnUi : CursorLockOff::NoPress;
			return;
		}
		_cursorLock = _pressPoint.value_or(*input);
	}
	if (_pivot && _pivotOnCursor && !_ground)
	{
		// Keep the cursor on the pivot as the user sees it. In the ground range the cursor stays at the place of the
		// press, as for a mouse look. The pivot moves on the screen only when the rig cannot keep it (the land, the
		// edge of the map) or when the game moves the camera.
		auto screen = game.ScreenPointOf(*_pivot);
		auto size = game.ScreenSize();
		if (screen && size && screen->z > 0.0f && screen->x >= 0.0f && screen->y >= 0.0f && screen->x <= size->x &&
		    screen->y <= size->y)
		{
			_cursorLock = WorldPoint {screen->x, screen->y, 0.0f};
		}
	}
	// The input position of the game is the mouse position plus an offset of the game. The rig moves the mouse so
	// that the input position is at the lock point.
	const auto offset = *input - *mouse;
	auto warped = game.WarpCursor(*_cursorLock - offset);
	if (!warped)
	{
		// The cursor stays free until the release, as with no lock.
		_cursorLock.reset();
		_cursorLockOff = CursorLockOff::WarpFailed;
		return;
	}
	// The game measures the turn of the next frame from the place where the cursor is now, and the hand stays on the
	// pivot. The pitch of the next frame is the move of the mouse from that place too.
	game.SetTurnCursor(*warped + offset);
	game.SetHandOverride(*_cursorLock);
	_lastMouseX = warped->x;
	_lastMouseY = warped->y;
	_trace.cursorLock = _cursorLock;
	_trace.warpedMouse = warped;
}

float CameraLoop::StepTurnGlide(float userTurn, bool middleHeld)
{
	const auto seconds = static_cast<float>(_settings.turnInertia);
	if (seconds <= 0.0f)
	{
		_turnGlide.Stop();
		_turnRate.Reset();
		_turnTracking = false;
		return 0.0f;
	}
	if (middleHeld || userTurn != 0.0f)
	{
		// The user turns: measure the rate of the turn over the last part of a second.
		_turnGlide.Stop();
		if (!_turnTracking)
		{
			_turnRate.Reset();
		}
		_turnRate.Add(userTurn, _deltaTime, k_GlideRateSeconds);
		_turnTracking = true;
		_trace.turnRate = _turnRate.rate;
		return 0.0f;
	}
	if (std::exchange(_turnTracking, false))
	{
		// The user released the button, or the turn of the keys stopped. The turn continues with the measured rate.
		static_cast<void>(_turnGlide.Start(_turnRate.rate, k_GlideStopDegrees));
		_turnRate.Reset();
	}
	const auto turn = _turnGlide.Step(_deltaTime, seconds, k_GlideStopDegrees);
	_trace.glideTurn = turn;
	return turn;
}

float CameraLoop::StepPitchGlide(float userRaise, bool middleHeld)
{
	const auto seconds = static_cast<float>(_settings.pitchInertia);
	if (seconds <= 0.0f)
	{
		_pitchGlide.Stop();
		_pitchRate.Reset();
		_pitchTracking = false;
		return 0.0f;
	}
	if (middleHeld)
	{
		// The user pitches: measure the rate of the pitch over the last part of a second.
		_pitchGlide.Stop();
		if (!_pitchTracking)
		{
			_pitchRate.Reset();
		}
		_pitchRate.Add(userRaise, _deltaTime, k_GlideRateSeconds);
		_pitchTracking = true;
		_trace.pitchRate = _pitchRate.rate;
		return 0.0f;
	}
	if (std::exchange(_pitchTracking, false))
	{
		// The user released the button. The pitch continues with the measured rate.
		static_cast<void>(_pitchGlide.Start(_pitchRate.rate, k_GlideStopDegrees));
		_pitchRate.Reset();
	}
	const auto raise = _pitchGlide.Step(_deltaTime, seconds, k_GlideStopDegrees);
	_trace.glidePitch = raise;
	return raise;
}

} // namespace moa_mod::camera
