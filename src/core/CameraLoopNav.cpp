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
constexpr int32_t k_RightMouseButton = 1;
/// The game holds the land under the cursor while the fade of its drag is above this value.
constexpr float k_DragFadeThreshold = 0.01f;
/// The smallest distance from the camera to the view centre that a flight uses, metres.
constexpr float k_SmallestViewDistance = 0.01f;
/// The smallest tilt of a zoom stage that the navigation uses to divide, degrees.
constexpr float k_FlattestNavigationTilt = 1.0f;
} // namespace

bool CameraLoop::FlyToSpot(const CameraSpot& spot)
{
	if (_settings.pitchMode != PitchMode::Hand)
	{
		return false;
	}
	_flightRequest = FlightRequest {.kind = FlightRequest::Kind::Spot, .spot = spot, .northDegrees = 0.0f};
	return true;
}

bool CameraLoop::ResetView(float northDegrees)
{
	if (_settings.pitchMode != PitchMode::Hand)
	{
		return false;
	}
	_flightRequest = FlightRequest {.kind = FlightRequest::Kind::Reset, .spot = {}, .northDegrees = northDegrees};
	return true;
}

std::optional<CameraSpot> CameraLoop::CurrentSpot() const
{
	if (!_rig || _stageTable.size() < 2)
	{
		return std::nullopt;
	}
	const auto& rig = *_rig;
	// The centre is on the view line, at the distance of the focus that the rig gives to the game.
	const auto distance = std::max(Length(GameStateFor(rig.position).focus - rig.position), k_SmallestViewDistance);
	return CameraSpot {.position = rig.position,
	                   .yaw = rig.yaw,
	                   .pitch = rig.pitch,
	                   .raise = _pitchValue,
	                   .stage = std::max(_zoom.value, 0.0f),
	                   .centre = rig.position + ViewDirection(rig.yaw, rig.pitch) * distance};
}

float CameraLoop::PitchFor(float raise, float stage) const
{
	const auto tilt = StageTiltOf(stage);
	const auto range = PitchRangeFor(stage, tilt, PitchLimitsFromSettings());
	const auto view = SplitPitch(raise, tilt, range);
	return view.tilt - view.lookUp;
}

std::optional<FlightView> CameraLoop::ShownView(CameraGame& game)
{
	const auto& rig = *_rig;
	const auto camera = ShownCamera();
	if (!game.SetCamera({.position = camera, .rotation = HandRigRotation(rig.yaw, rig.pitch)}))
	{
		return std::nullopt;
	}
	// The point at the view centre is the land at the screen centre. With no land there (the sky), it is on the view
	// line at the distance of the focus that the rig gives to the game.
	auto distance = Length(GameStateFor(camera).focus - camera);
	if (auto screen = game.ScreenSize())
	{
		if (auto land = game.LandUnderScreenPoint({screen->x * 0.5f, screen->y * 0.5f, 0.0f}))
		{
			const auto toLand = Length(*land - camera);
			if (toLand > k_SmallestViewDistance)
			{
				distance = toLand;
			}
		}
	}
	// A flight from the ground range starts at the closest zoom stage of the game.
	return ViewOfCamera(camera, rig.yaw, rig.pitch, _pitchValue, std::max(_zoom.value, 0.0f),
	                    std::max(distance, k_SmallestViewDistance));
}

void CameraLoop::StartFlight(const FlightView& from, FlightView to)
{
	auto& rig = *_rig;
	if (_focusBox)
	{
		// The flight never ends out of the map. The camera moves with its centre.
		const auto clamped = ClampToBox(to.centre, *_focusBox);
		to.centre = clamped;
	}
	// The rig starts from the camera that the user sees, and the spring arm starts again. The camera keeps its speed:
	// the chase lets it go into the path of the flight.
	rig.position = from.Camera();
	_chase.lag = {};
	_grip.reset();
	_turnInput.Reset();
	_pitchInput.Reset();
	_ground.reset();
	ResetArm();
	_pivot.reset();
	_pivotLimited = false;
	StopGlide();
	StopTurnGlides();
	_flight = CameraFlight {.from = from, .to = to, .seconds = static_cast<float>(_settings.flyToSeconds), .elapsed = 0.0f};
	_trace.flightStarted = true;
}

bool CameraLoop::StartFlights(CameraGame& game, bool leftPressed, StageLimits limits)
{
	const auto gameDistance = [this](float stage) {
		const auto tilt = std::max(StageTiltOf(stage), k_FlattestNavigationTilt);
		return StageHeightOf(stage) / std::sin(tilt * k_DegreesToRadians);
	};
	if (_flightRequest)
	{
		const auto request = *_flightRequest;
		_flightRequest.reset();
		auto from = ShownView(game);
		if (!from)
		{
			return false;
		}
		FlightView to;
		if (request.kind == FlightRequest::Kind::Spot)
		{
			const auto& spot = request.spot;
			to = {.centre = spot.centre,
			      .yaw = spot.yaw,
			      .pitch = spot.pitch,
			      .raise = spot.raise,
			      .stage = std::clamp(spot.stage, limits.minimum, limits.maximum),
			      .distance = std::max(Length(spot.position - spot.centre), k_SmallestViewDistance)};
		}
		else
		{
			// The view of the game at the current zoom: the tilt of the stage and the distance of the stage, about the
			// point at the view centre, with the heading to the north.
			to = {.centre = from->centre,
			      .yaw = WrapDegrees(request.northDegrees),
			      .pitch = PitchFor(0.0f, from->stage),
			      .raise = 0.0f,
			      .stage = from->stage,
			      .distance = gameDistance(from->stage)};
		}
		StartFlight(*from, to);
		game.Debug(
		    std::format("camera mod: the camera flies to ({:.1f}, {:.1f}, {:.1f})", to.centre.x, to.centre.y, to.centre.z));
	}
	if (!_settings.flyTo || !leftPressed)
	{
		return true;
	}
	// The game zooms to a stage after a double click on the land. It finds the double click with its own rules (time,
	// place, the hand, the UI and the objects). The loop flies to the land instead of that zoom.
	auto zoomTo = game.ZoomToStage();
	if (!zoomTo || *zoomTo < 0.0f || !_zoomToStageBefore || *_zoomToStageBefore >= 0.0f)
	{
		return true;
	}
	auto from = ShownView(game);
	if (!from)
	{
		return false;
	}
	auto land = LandUnderCursor(game);
	if (!land)
	{
		// The land is not under the cursor in the view of the user. The game zooms as before.
		return true;
	}
	game.SetZoomToStage(-1.0f);
	auto to = *from;
	to.centre = *land;
	// From the ground range the view centre is far away, so the flight goes to the closest zoom stage of the game.
	if (_settings.flyToStage >= 0.0 || _ground)
	{
		const auto stage = _settings.flyToStage >= 0.0 ? static_cast<float>(_settings.flyToStage) : 0.0f;
		to.stage = std::clamp(stage, limits.minimum, limits.maximum);
		to.raise = 0.0f;
		to.pitch = PitchFor(0.0f, to.stage);
		to.distance = gameDistance(to.stage);
	}
	StartFlight(*from, to);
	game.Debug(
	    std::format("camera mod: a double click flies the camera to ({:.1f}, {:.1f}, {:.1f})", land->x, land->y, land->z));
	return true;
}

bool CameraLoop::StepFlight(CameraGame& game, bool keepAboveLand)
{
	auto& rig = *_rig;
	auto& flight = *_flight;
	// The spring arm keeps the part of its length that the obstacles took.
	auto armRatio = 1.0f;
	if (_armPivot)
	{
		const auto desired = Length(rig.position - *_armPivot);
		armRatio = desired > k_SmallestViewDistance ? std::clamp(_armLength / desired, 0.0f, 1.0f) : 1.0f;
	}
	static_cast<void>(flight.Step(_deltaTime));
	const auto view = flight.At(_stageTable, [this](float raise, float stage) { return PitchFor(raise, stage); });
	rig.position = view.Camera();
	rig.yaw = view.yaw;
	rig.pitch = view.pitch;
	// The zoom, the pitch control and the heading follow the flight, so a stop of the flight leaves a view that the
	// user controls as before.
	_zoom.Reset(view.stage);
	_lastStage = view.stage;
	_pitchTarget = view.raise;
	_pitchValue = view.raise;
	_heading.Reset(view.yaw);
	const auto tilt = StageTiltOf(view.stage);
	_range = PitchRangeFor(view.stage, tilt, PitchLimitsFromSettings());
	_view = SplitPitch(view.raise, tilt, _range);
	_level = view.centre.y;
	_pivot.reset();
	_pivotLimited = false;
	_trace.flight = flight.Done();
	_trace.raise = _pitchValue;
	_trace.raiseTarget = _pitchTarget;
	_trace.viewTilt = rig.pitch;
	_trace.zoomHeight = StageHeightOf(view.stage);
	_trace.pivotKept = false;

	if (_settings.springArm)
	{
		// The spring arm turns about the point at the view centre, so the obstacles move the camera on the view line.
		_armPivot = view.centre;
		_armLength = armRatio * Length(rig.position - view.centre);
		// The flight sets the desired distance in each frame.
		_desired = Length(rig.position - view.centre);
		_trace.desiredChange = 2;
		// A flight is not a gesture about a pivot: only the camera ball and the floor keep the camera above the land.
		if (!StepArm(game, keepAboveLand, false))
		{
			return false;
		}
	}
	else if (keepAboveLand)
	{
		auto ground = game.HighestGround(rig.position);
		if (!ground)
		{
			return false;
		}
		rig.position.y = std::max(rig.position.y, *ground + static_cast<float>(_settings.groundClearance));
	}
	if (flight.Finished())
	{
		_flight.reset();
	}
	return true;
}

WorldPoint CameraLoop::SoftenGameShift(CameraGame& game, WorldPoint shift)
{
	if (_settings.edgeMargin <= 0.0 || !_focusBox || Length(shift) <= 0.0f)
	{
		return shift;
	}
	// The land drag holds the land under the cursor, and the game holds it again in its next frame. So the soft edge
	// does not act on the land drag. The game stops the drag at the edge of the map.
	// The land drag of the hand rig is not in the move of the game.
	auto right = game.MouseButton(k_RightMouseButton);
	if (!LandGripOn() && ((right && *right) || game.Drag().fade > k_DragFadeThreshold))
	{
		return shift;
	}
	auto fieldOfView = game.FieldOfView();
	auto screen = game.ScreenSize();
	if (!fieldOfView || !screen || screen->y <= 0.0f)
	{
		return shift;
	}
	// The margin is a part of the width of the view, so that the pan slows down in the same time at each zoom.
	const auto width = ViewWidth(Length(TargetCamera() - _focusWritten), *fieldOfView, screen->x / screen->y);
	const auto margin = static_cast<float>(_settings.edgeMargin) * width;
	const auto soft = SoftEdgeMove(_focusWritten, shift, *_focusBox, margin);
	const auto removed = shift - soft;
	if (Length(removed) > 0.0f)
	{
		_trace.edgeSoftened = removed;
	}
	return soft;
}

} // namespace moa_mod::camera
